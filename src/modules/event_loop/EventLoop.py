# 源片段：src/modules/event_loop/EventLoop.py（构建期由 scripts/assemble_py.py 合并，请勿单独导入）



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


class EventLoopPkg:
    def __init__(self, event_loop_name="", default_service_func=None, msg_name=""):
        self.m_eventloopName = event_loop_name
        self.m_defaultServiceFunc = default_service_func
        self.m_msgName = msg_name


class EventLoopQuery:
    # 事件循环单查载荷（/YomkEventLoop/loop 专用）：
    # 名称 + 队列头 tag 采样数，类型化字段无解析歧义
    def __init__(self, loop_name="", tag_count=3):
        self.m_loopName = loop_name   # 循环名（按字面量精确匹配，不做任何切分）
        self.m_tagCount = tag_count   # 队列头采样条数（缺省 3，与全量内省缺省一致）


class EventLoopInfo:
    # 事件循环内省元信息（/YomkEventLoop/loop 与 /YomkEventLoop/all 返回数据）
    def __init__(self, loop_name="", running=False, pending=0,
                 has_default_func=False, msg_name="", next_tags=None):
        self.loop_name = loop_name        # 循环名
        self.running = running            # worker 是否运行中
        self.pending = pending            # 积压事件数
        self.has_default_func = has_default_func  # 是否注册默认处理函数
        self.msg_name = msg_name          # 默认处理函数期望消息类型名（可空）
        # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）
        self.next_tags = next_tags if next_tags is not None else []


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
                log.error("EventLoop: event is null, please check event")
                continue
            # 异常吞噬后仍无条件触发等待回调
            try:
                event.handle()
            except Exception as e:
                log.error(
                    f"EventLoop: {event.m_eventLoopName} exec event id: {event.m_eventId} caught, what: {e}")
            except BaseException:
                log.error("EventLoop: unknown exception caught")
            if event.m_waitCallback:
                event.m_waitCallback()

    def start(self):
        if self.m_running:
            log.error("EventLoop already running, please do not start it again")
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
        # 判序先判运行态（返 2，与入队成功 0/事件为空 1 区分，供上层映射错误码），后判事件
        if not self.m_running:
            log.error("EventLoop not running, please start event loop.")
            return 2

        if not event:
            log.error("EventLoop: event is null, please check event")
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
        # 前置检查与 post 全同
        if not self.m_running:
            log.error("EventLoop not running, please start event loop.")
            return 2

        if not event:
            log.error("EventLoop: event is null, please check event")
            return 1

        # 自 join 防护读取持队列锁：与 start 持锁创建线程互斥串行，避免无锁并发读写
        with self.m_queueMutex:
            in_worker = self.m_thread is not None and threading.get_ident() == self.m_thread.ident
        if in_worker:
            log.error(
                "EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve "
                "deadlock")
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
        # 队列锁内取数；队列头起最多 tag_count 条事件 tag，存原值（空串保留，展示层 "-" 占位由 info_line 处理）
        with self.m_queueMutex:
            info.running = self.m_running
            info.pending = self.m_event_queue.qsize()
            info.has_default_func = self.m_defaultServiceFunc is not None
            info.msg_name = self.m_defaultMsgName
            for event in list(self.m_event_queue.queue)[:tag_count]:
                info.next_tags.append(event.m_tag if event else "")

    def info_line(self, loop_name, tag_count):
        # 内省元信息行：结构化快照取数，展示层拼行；
        # 格式 "名 running:on|off pending:N defaultFunc:on|off [类型名] nextNEventTag(N): tag1, tag2..."
        info = EventLoopInfo()
        self.info_snapshot(info, tag_count)

        # 队首最多列出 tag_count 个事件 tag，队列不足则全部列出；空 tag 显示 "-" 占位
        tag_list = ", ".join(tag if tag else "-" for tag in info.next_tags)
        return (loop_name + (" running:on" if info.running else " running:off") +
                " pending:" + str(info.pending) +
                (" defaultFunc:on" if info.has_default_func else " defaultFunc:off") +
                ((" [" + info.msg_name + "]") if info.has_default_func and info.msg_name else "") +
                " nextNEventTag(" + str(tag_count) + "): " + tag_list)
