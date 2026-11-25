from readerwriterlock import rwlock

from yomk._diag import warn as diag_warn
from yomk.event_loop.EventLoop import EventLoop
from yomk.msgs.event_loop import Event, EventLoopInfo, EventLoopInfoList, EventLoopOpts, EventLoopQuery
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.YomkMsg import unpack
from yomk.server import YomkService
from yomk.url.event_loop import (
    LOOP_ALL_DESC,
    LOOP_DESTROY_DESC,
    LOOP_LOOP_DESC,
    LOOP_LOOPS_DESC,
    LOOP_POST_DESC,
    LOOP_POST_WAIT_DESC,
    LOOP_START_DESC,
    LOOP_STOP_DESC,
)


class YomkEventLoop(YomkService):
    # 事件循环内置服务：链式 builder 的全部终结都经 api_request 到达本域端点。
    # 诊断契约：not-found 一律 WARN 并携带具体循环名；unpack 失败不打诊断；
    # 循环未运行只经返回码体现；成功路径无诊断。
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkEventLoop")
        self.m_eventLoop = {}
        self.m_eventLoopMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/start", self.start, EventLoopOpts.TYPE_NAME,
                          LOOP_START_DESC)
        self.install_func("/stop", self.stop, String.TYPE_NAME, LOOP_STOP_DESC)
        self.install_func("/post", self.post, Event.TYPE_NAME, LOOP_POST_DESC)
        self.install_func("/post_wait", self.post_wait, Event.TYPE_NAME,
                          LOOP_POST_WAIT_DESC)
        self.install_func("/destroy", self.destroy, String.TYPE_NAME,
                          LOOP_DESTROY_DESC)
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/loops", self.loops, "", LOOP_LOOPS_DESC)
        self.install_func("/loop", self.loop_info, EventLoopQuery.TYPE_NAME,
                          LOOP_LOOP_DESC)
        self.install_func("/all", self.list_all, "", LOOP_ALL_DESC)
        return 0

    def _not_found(self, loop_name):
        # not-found 契约：WARN 携具体名 + 同串 Response（stop/destroy/post/
        # post_wait/loopInfo 五处共用）
        diag_warn("event loop '" + loop_name + "' does not exist")
        return Response(ResStatus.eNo,
                        "event loop '" + loop_name + "' does not exist")

    def start(self, msg):
        r = unpack(msg, EventLoopOpts)
        if not r:
            return Response(ResStatus.eNo, r.error)
        # 空名是合法 map 键
        with self.m_eventLoopMutex.gen_wlock():
            if r.msg.m_eventLoopName in self.m_eventLoop:
                # 已存在仅重启，不重设默认函数
                self.m_eventLoop[r.msg.m_eventLoopName].start()
                return Response(ResStatus.eOk, "event loop start success")
            event_loop = EventLoop()
            event_loop.set_default_service_func(
                r.msg.m_defaultServiceFunc, r.msg.m_msgName)
            event_loop.start()
            self.m_eventLoop[r.msg.m_eventLoopName] = event_loop
            return Response(ResStatus.eOk, "event loop start success")

    def stop(self, msg):
        r = unpack(msg, String)
        if not r:
            return Response(ResStatus.eNo, r.error)
        with self.m_eventLoopMutex.gen_rlock():
            if r.msg.value in self.m_eventLoop:
                self.m_eventLoop[r.msg.value].stop()
                return Response(ResStatus.eOk, "event loop stop success")
        return self._not_found(r.msg.value)

    def destroy(self, msg):
        r = unpack(msg, String)
        if not r:
            return Response(ResStatus.eNo, r.error)
        with self.m_eventLoopMutex.gen_wlock():
            if r.msg.value in self.m_eventLoop:
                # 调底层 destroy（stop+清队列+释放等待者）后出表
                self.m_eventLoop[r.msg.value].destroy()
                del self.m_eventLoop[r.msg.value]
                return Response(ResStatus.eOk, "event loop destroy success")
        return self._not_found(r.msg.value)

    def post(self, msg):
        r = unpack(msg, Event)
        if not r:
            return Response(ResStatus.eNo, r.error)
        with self.m_eventLoopMutex.gen_rlock():
            loop = self.m_eventLoop.get(r.msg.m_eventLoopName)
            if loop is None:
                return self._not_found(r.msg.m_eventLoopName)
            # 内层返回码映射：0 成功；其余（实际仅 2=循环未运行被拒，
            # 事件为空不可达）统一 eNo，文案携循环名
            rc = loop.post(r.msg)
            if rc != 0:
                return Response(
                    ResStatus.eNo,
                    "event loop '" + r.msg.m_eventLoopName
                    + "' not running, post rejected")
            return Response(ResStatus.eOk, "event loop post success")

    def post_wait(self, msg):
        r = unpack(msg, Event)
        if not r:
            return Response(ResStatus.eNo, r.error)
        with self.m_eventLoopMutex.gen_rlock():
            loop = self.m_eventLoop.get(r.msg.m_eventLoopName)
            if loop is None:
                return self._not_found(r.msg.m_eventLoopName)
            # 内层返回码映射：0 成功（回传 event 携带执行结果）；其余统一
            # eNo（未入队未执行，不回传），文案携循环名
            rc = loop.post_wait(r.msg)
            if rc != 0:
                return Response(
                    ResStatus.eNo,
                    "event loop '" + r.msg.m_eventLoopName
                    + "' not running, post wait rejected")
            return Response(ResStatus.eOk, "event loop post wait success", r.msg)

    def loops(self, msg):
        # 内省：列出全部事件循环名（sorted 字典序，
        # 同 FunctionPool names / Context keys 内省先例）
        with self.m_eventLoopMutex.gen_rlock():
            loop_names = sorted(self.m_eventLoop.keys())
        return Response(ResStatus.eOk, "ok", StringArray(loop_names))

    def loop_info(self, msg):
        # 内省：单个事件循环元信息，收结构化 EventLoopQuery：unpack 失败返回
        # eNo；名称按字面量精确匹配、不做任何切分；采样数越界回退默认 3（不抛不崩）
        r = unpack(msg, EventLoopQuery)
        if not r:
            return Response(ResStatus.eNo, r.error)
        loop_name = r.msg.m_loopName
        # 归一化采样数：非 int（含 bool）或超 2^64-1 上限回退默认 3，与全量内省缺省一致
        tag_count = 3
        if isinstance(r.msg.m_tagCount, int) and not isinstance(r.msg.m_tagCount, bool) \
                and 0 <= r.msg.m_tagCount <= 2 ** 64 - 1:
            tag_count = r.msg.m_tagCount

        with self.m_eventLoopMutex.gen_rlock():
            event_loop = self.m_eventLoop.get(loop_name)
        if event_loop is None:
            return self._not_found(loop_name)

        # 命中：结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        info = EventLoopInfo(loop_name)
        event_loop.info_snapshot(info, tag_count)
        return Response(ResStatus.eOk, "ok", info)

    def list_all(self, msg):
        # 内省：全部事件循环元信息，锁内拷贝快照、锁外逐循环取结构化快照
        # （msg 恒 "ok"，数据走 data；next_tags 取 3 条与单查缺省一致）
        with self.m_eventLoopMutex.gen_rlock():
            # key 取名序：sorted 字典序（同 loops() 先例）
            pairs = sorted(self.m_eventLoop.items(), key=lambda kv: kv[0])
        infos = []
        for name, loop in pairs:
            info = EventLoopInfo(name)
            loop.info_snapshot(info, 3)
            infos.append(info)
        return Response(ResStatus.eOk, "ok", EventLoopInfoList(infos))

    def deinit(self):
        # 收尾残留 loop：写锁内快照并清表，锁外逐个 destroy
        # （stop+清队列+释放等待者）；锁外清理模式与 shutdown 先例一致
        with self.m_eventLoopMutex.gen_wlock():
            loops = list(self.m_eventLoop.values())
            self.m_eventLoop.clear()
        for loop in loops:
            loop.destroy()
