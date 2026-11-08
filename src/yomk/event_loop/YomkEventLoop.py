from readerwriterlock import rwlock

from yomk.event_loop._diag import error as diag_error
from yomk.event_loop.EventLoop import Event, EventLoop
from yomk.msg.event_loop import EventLoopInfo, EventLoopPkg, EventLoopQuery
from yomk.msg.server import ResStatus, Response
from yomk.server import YomkService


class YomkEventLoop(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkEventLoop")
        self.m_eventLoop = {}
        self.m_eventLoopMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/start", self.start, "EventLoop")
        self.install_func("/stop", self.stop, "String")
        self.install_func("/post", self.post, "Event")
        self.install_func("/post_wait", self.post_wait, "Event")
        self.install_func("/destroy", self.destroy, "String")
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/loops", self.loops)
        self.install_func("/loop", self.loop_info, "EventLoopQuery")
        self.install_func("/all", self.list_all)
        return 0

    def start(self, pkg: EventLoopPkg):
        if not isinstance(pkg, EventLoopPkg):
            raise TypeError("pkg must be EventLoopPkg")
        # 空名是合法 map 键
        with self.m_eventLoopMutex.gen_wlock():
            if pkg.m_eventloopName in self.m_eventLoop:
                # 已存在仅重启，不重设默认函数
                self.m_eventLoop[pkg.m_eventloopName].start()
                return Response(ResStatus.eOk, "event loop start success")
            event_loop = EventLoop()
            event_loop.set_default_service_func(
                pkg.m_defaultServiceFunc, pkg.m_msgName)
            event_loop.start()
            self.m_eventLoop[pkg.m_eventloopName] = event_loop
            return Response(ResStatus.eOk, "event loop start success")

    def stop(self, pkg):
        with self.m_eventLoopMutex.gen_rlock():
            if pkg in self.m_eventLoop:
                self.m_eventLoop[pkg].stop()
                return Response(ResStatus.eOk, "event loop stop success")
            else:
                diag_error("event loop: " + pkg +
                           " not exist, please check event loop name")
                return Response(ResStatus.eNo, "event loop not exist")

    def destroy(self, pkg):
        with self.m_eventLoopMutex.gen_wlock():
            if pkg in self.m_eventLoop:
                # 调底层 destroy（stop+清队列+释放等待者）后出表
                self.m_eventLoop[pkg].destroy()
                del self.m_eventLoop[pkg]
                return Response(ResStatus.eOk, "event loop destroy success")
            else:
                diag_error("event loop: " + pkg +
                           " not exist, please check event loop name")
                return Response(ResStatus.eNo, "event loop not exist")

    def post(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                # 内层返回码映射：0 成功；其余（实际仅 2=循环未运行被拒）统一 eNo
                rc = self.m_eventLoop[pkg.m_eventLoopName].post(pkg)
                if rc != 0:
                    return Response(ResStatus.eNo, "event loop not running, post rejected")
                return Response(ResStatus.eOk, "event loop post success")
            else:
                diag_error("event loop: " + pkg.m_eventLoopName +
                           " not exist, please check event loop name")
                return Response(ResStatus.eNo, "event loop not exist")

    def post_wait(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                # 内层返回码映射：0 成功（回传 event 携带执行结果）；其余统一 eNo（未入队未执行，不回传）
                rc = self.m_eventLoop[pkg.m_eventLoopName].post_wait(pkg)
                if rc != 0:
                    return Response(ResStatus.eNo, "event loop not running, post wait rejected")
                return Response(ResStatus.eOk, "event loop post wait success", pkg)
            else:
                diag_error("event loop: " + pkg.m_eventLoopName +
                           " not exist, please check event loop name")
                return Response(ResStatus.eNo, "event loop not exist")

    def loops(self, pkg):
        # 内省：列出全部事件循环名（sorted 字典序，
        # 同 FunctionPool names / Context keys 内省先例）
        with self.m_eventLoopMutex.gen_rlock():
            loop_names = sorted(self.m_eventLoop.keys())
        return Response(ResStatus.eOk, "ok", loop_names)

    def loop_info(self, pkg):
        # 内省：单个事件循环元信息，收结构化 EventLoopQuery：unpack 失败返回 eNo；
        # 名称按字面量精确匹配、不做任何切分；采样数越界回退默认 3（不抛不崩）
        if not isinstance(pkg, EventLoopQuery):
            return Response(ResStatus.eNo, "pkg is null or pkg is not EventLoopQuery")
        loop_name = pkg.m_loopName
        # 归一化采样数：非 int（含 bool）或超 2^64-1 上限回退默认 3，与全量内省缺省一致
        tag_count = 3
        if isinstance(pkg.m_tagCount, int) and not isinstance(pkg.m_tagCount, bool) \
                and 0 <= pkg.m_tagCount <= 2 ** 64 - 1:
            tag_count = pkg.m_tagCount

        with self.m_eventLoopMutex.gen_rlock():
            if loop_name in self.m_eventLoop:
                # 锁内拷循环对象，锁外取数（info_snapshot 自锁队列锁）
                event_loop = self.m_eventLoop[loop_name]
            else:
                diag_error("event loop: " + loop_name +
                           " not exist, please check event loop name")
                return Response(ResStatus.eNo, "event loop not exist")

        # 命中：结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        info = EventLoopInfo(loop_name)
        event_loop.info_snapshot(info, tag_count)
        return Response(ResStatus.eOk, "ok", info)

    def list_all(self, pkg):
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
        return Response(ResStatus.eOk, "ok", infos)

    def deinit(self):
        # 收尾残留 loop：写锁内快照并清表，锁外逐个 destroy
        # （stop+清队列+释放等待者）；锁外清理模式与 shutdown 先例一致
        with self.m_eventLoopMutex.gen_wlock():
            loops = list(self.m_eventLoop.values())
            self.m_eventLoop.clear()
        for loop in loops:
            loop.destroy()