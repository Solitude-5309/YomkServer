import queue
import threading

from yomk._diag import error as diag_error
from yomk._diag import info as diag_info


class Event:
    def __init__(self, event_loop_name="", pkg=None, func=None, tag=""):
        self.m_eventLoopName = event_loop_name
        self.m_pkg = pkg
        self.m_serviceFunc = func
        self.m_eventId = 0
        self.m_response = None
        self.m_waitCallback = None
        self.m_tag = tag

    def handle(self):
        if not self.m_serviceFunc:
            return
        self.m_response = self.m_serviceFunc(self.m_pkg)


class EventLoop:
    def __init__(self):
        self.m_event_queue = queue.Queue()
        self.m_queueMutex = threading.Lock()
        self.m_condition = threading.Condition(self.m_queueMutex)
        self.m_thread = None
        self.m_running = False
        self.m_event_id = 1
        self.m_defaultServiceFunc = None
        self.m_defaultMsgName = ""

    def set_default_service_func(self, func, msg_name=""):
        # 双参msg_name 同存，infoLine 内省依赖
        self.m_defaultServiceFunc = func
        self.m_defaultMsgName = msg_name

    def run(self):
        while self.m_running:
            with self.m_queueMutex:
                self.m_condition.wait_for(
                    lambda: not self.m_event_queue.empty() or not self.m_running)

                if not self.m_running:
                    break

                try:
                    event = self.m_event_queue.get_nowait()
                except queue.Empty:
                    continue

            if not event:
                # 空事件静默跳过（对齐 C++ run()：无诊断）
                continue
            # 异常吞噬后仍无条件触发等待回调
            try:
                event.handle()
            except Exception as e:
                diag_error(
                    f"EventLoop: {event.m_eventLoopName} exec event id: {event.m_eventId} caught, what: {e}")
            except BaseException:
                diag_error("EventLoop: unknown exception caught")
            if event.m_waitCallback:
                event.m_waitCallback()

    def start(self):
        if self.m_running:
            # 幂等重启（对齐 C++）：INFO 级，非错误
            diag_info("event loop already running, start is idempotent")
            return 0
        # 持队列锁创建线程
        with self.m_queueMutex:
            self.m_running = True
            self.m_thread = threading.Thread(target=self.run)
            self.m_thread.start()
        return 0

    def stop(self):
        if not self.m_running:
            return 0
        self.m_running = False
        # 仅退线程不清队列：未执行事件保留，待 start 续跑
        with self.m_queueMutex:
            self.m_condition.notify_all()
        if self.m_thread and self.m_thread.is_alive():
            self.m_thread.join()
        return 0

    def destroy(self):
        # stop 后锁内清空队列，锁外逐个触发被丢事件的等待回调（释放 postWait 等待者）；
        # 锁外调回调避免与 postWait 持临时锁回调构成 ABBA 死锁；幂等可重复进入
        self.stop()
        discarded = []
        with self.m_queueMutex:
            while not self.m_event_queue.empty():
                discarded.append(self.m_event_queue.get_nowait())
        for event in discarded:
            if event and event.m_waitCallback:
                event.m_waitCallback()
        return 0

    def __del__(self):
        # 析构兜底：与显式 destroy 共享释放契约（丢弃事件触发等待回调）；
        # try 防护解释器关闭期属性缺失；引用环场景需 gc.collect() 显式触发析构
        try:
            self.destroy()
        except Exception:
            pass

    def post(self, event):
        # 判序先判运行态（返 2，与入队成功 0/事件为空 1 区分，供上层映射
        # 错误码）；未运行/事件为空无诊断（对齐 C++ rc2/rc1 均 NO diag）
        if not self.m_running:
            return 2

        if not event:
            return 1

        with self.m_queueMutex:
            self.m_event_id += 1
            event.m_eventId = self.m_event_id
            if event.m_serviceFunc is None:
                event.m_serviceFunc = self.m_defaultServiceFunc
            self.m_event_queue.put(event)
            self.m_condition.notify_all()

        return 0

    def post_wait(self, event):
        # 前置检查与 post 全同（未运行/事件为空均无诊断）
        if not self.m_running:
            return 2

        if not event:
            return 1

        # 自 join 防护读取持队列锁：与 start 持锁创建线程互斥串行，避免无锁并发读写
        with self.m_queueMutex:
            in_worker = self.m_thread is not None and threading.get_ident() == self.m_thread.ident
        if in_worker:
            # C-2 已采纳：文案补循环名（事件自带字段）+ 去 "is not allowed"
            # 冗余从句，前缀对齐域内 "EventLoop: <名>" 格式（C++ 侧同步同文案）
            diag_error(
                "EventLoop: " + event.m_eventLoopName +
                " post wait in worker thread detected deadlock, directly execute current event to resolve deadlock")
            event.handle()
            return 0

        tmp_mutex = threading.Lock()
        tmp_condition = threading.Condition(tmp_mutex)
        notified = [False]

        def callback():
            with tmp_mutex:
                notified[0] = True
                tmp_condition.notify_all()

        with tmp_condition:
            event.m_waitCallback = callback
            # TOCTOU 防护：仅在实际入队成功后才等待——检查与入队之间循环被停止/销毁时
            # post 返回非零，直接返回避免等待一个永不被执行/触发的回调
            rc = self.post(event)
            if rc != 0:
                return rc
            while not notified[0]:
                tmp_condition.wait()

        return 0

    def info_snapshot(self, info, tag_count):
        # 结构化内省快照：填充事件循环元信息（/YomkEventLoop/loop 返回数据）；
        # 队列锁内取数；队列头起最多 tag_count 条事件 tag，存原值（空串保留，
        # 展示层 "-" 占位等拼行格式由用户侧定义，框架只回结构化数据）
        with self.m_queueMutex:
            info.running = self.m_running
            info.pending = self.m_event_queue.qsize()
            info.has_default_func = self.m_defaultServiceFunc is not None
            info.msg_name = self.m_defaultMsgName
            for event in list(self.m_event_queue.queue)[:tag_count]:
                info.next_tags.append(event.m_tag if event else "")