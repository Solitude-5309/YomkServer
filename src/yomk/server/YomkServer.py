import os
from concurrent.futures import ThreadPoolExecutor

from readerwriterlock import rwlock

from yomk.msg.server import ResStatus, Response
from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn
from yomk.server.YomkService import YomkService


class YomkServer:
    def __init__(self, async_thread_count=0):
        self.services: dict[str, YomkService] = {}
        self.rwlock_services = rwlock.RWLockFair()
        self.shutdown_done: bool = False
        if async_thread_count == 0:
            hw = os.cpu_count()
            # hw 为 0(或 None) 兜底 2，否则取一半向上取整
            async_thread_count = 2 if not hw else (hw + 1) // 2
        self.executor = ThreadPoolExecutor(async_thread_count)

    @classmethod
    def create(cls, async_thread_count=0):
        # 创建 YomkServer 实例，async_thread_count 为异步请求线程池大小（0 表示自动检测）
        return cls(async_thread_count)

    def start_service(self, srv_names):
        # 延迟导入内置服务，避免 server 在模块级反向依赖各扩展域；
        # 内部类走子模块路径（U-12 公开面收敛后包根不再导出内部类）
        from yomk.ctx.YomkContext import YomkContext
        from yomk.event_loop.YomkEventLoop import YomkEventLoop
        from yomk.fn_pool.YomkFunctionPool import YomkFunctionPool
        from yomk.log.YomkLogger import YomkLogger
        from yomk.srv_info.YomkServerInfo import YomkServerInfo

        # 内置服务映射表（键名带 / 前缀）
        service_creators = {
            "/YomkContext": YomkContext,
            "/YomkFunctionPool": YomkFunctionPool,
            "/YomkEventLoop": YomkEventLoop,
            "/YomkLogger": YomkLogger,
            "/YomkServerInfo": YomkServerInfo,
        }
        for srv_name in srv_names:
            srv_cls = service_creators.get(srv_name)
            if srv_cls is None:
                diag_warn("yomk does not support service: " + srv_name)
                continue
            self.add_service(srv_cls(self))

        return 0

    def new_service(self, srv_cls, srv_name=""):
        # 创建并注册指定类型的服务
        srv = srv_cls(self)
        if srv_name:
            srv.set_name(srv_name)
        return self.add_service(srv)

    def add_service(self, service):
        if not isinstance(service, YomkService):
            diag_warn("service is null, please check the service")
            return -1

        old_srv = None
        with self.rwlock_services.gen_wlock():
            if (service.get_name() in self.services):
                # C-5 已采纳：同名替换即覆盖既有注册，级别 WARN（误覆盖默认可见）；
                # add() 返回 int 不携带该信息，替换的可见性另由旧服务 mark_deleted
                # 的删除 WARN（C-3）与本 WARN 双重承担
                diag_warn(
                    f"service '{service.get_name()}' already exists, replace with current service")
            old_srv = self.services.get(service.get_name())
            self.services[service.get_name()] = service
            # 入表即锁定服务名：注册后改名被拒绝，保证服务名与 service map 键一致
            service.mark_registered()

        # 锁外对旧服务置位注销标志并 deinit：同名替换语义，替换前旧服务仍在表中；
        # 置位后旧服务弱绑定回调立即丢弃（删除即停）
        if old_srv is not None:
            old_srv.mark_deleted()
            old_srv.deinit()

        # init 抛异常视同失败：捕获后走统一回滚，避免半初始化服务残留在表；
        # init 未显式返回（None）等价成功
        try:
            init_ret = service.init()
        except Exception as e:
            diag_error("service init exception: " +
                       service.get_name() + ", what: " + str(e))
            init_ret = -1

        if init_ret is not None and init_ret != 0:
            diag_error("service init error: " + service.get_name())
            self.del_service(service.get_name())
            return -1
        return 0

    def del_service(self, srv_name):
        srv = None
        with self.rwlock_services.gen_wlock():
            if (srv_name not in self.services):
                diag_warn("service not found: " + srv_name + ", delete ignored")
                return -1
            srv = self.services.pop(srv_name)

        # 锁外置位注销标志并执行 deinit，与 shutdown 保持一致的锁外清理模式；
        # 置位后已注册弱绑定回调立即丢弃（删除即停）
        if srv is not None:
            srv.mark_deleted()
            srv.deinit()
        return 0

    def shutdown(self):
        # 幂等：重复调用直接返回，不会二次排空与 deinit
        if self.shutdown_done:
            return
        self.shutdown_done = True

        # 先停异步请求池（拒新 -> 排空）
        self.executor.shutdown(wait=True)

        with self.rwlock_services.gen_wlock():
            srvs = list(self.services.values())
            self.services.clear()

        # 锁外逐个 deinit，与 del_service 保持一致的锁外清理模式
        for srv in srvs:
            srv.deinit()

    def service_names(self):
        with self.rwlock_services.gen_rlock():
            # 字典序输出（对齐 C++ std::map 迭代序）
            return sorted(self.services.keys())

    def service_func_infos(self, srv_name):
        with self.rwlock_services.gen_rlock():
            srv = self.services.get(srv_name)
        if srv is None:
            diag_warn("service not found: " + srv_name +
                      ", please start the service")
            return {}
        return srv.func_infos()

    def _parse_request_url(self, url):
        # 解析请求 url 为服务名与函数名（/ServiceName/func_name）；
        # 失败时返回 err_msg（供 request 构造响应，供 async_request 直接丢弃）
        if not url or url[0] != "/":
            return "", "", "url parse error: " + str(url) + ", please start with /"
        pos_end = url.find("/", 1)
        if pos_end == -1:
            return "", "", "url parse error: " + url + ", no service name"
        srv_name = url[:pos_end]
        if not srv_name:
            # C-4 已采纳：文案补具体值，对齐同函数其余分支 "url parse error: <url>, <原因>" 格式
            return "", "", "url parse error: " + url + ", srv name is empty"
        func_name = url[pos_end:]
        if not func_name:
            return "", "", "url parse error: " + url + ", function name is empty"
        return srv_name, func_name, None

    def request(self, url, pkg=None):
        srv_name, func_name, err_msg = self._parse_request_url(url)
        if err_msg:
            diag_warn(err_msg)
            return Response(ResStatus.eNo, err_msg)

        with self.rwlock_services.gen_rlock():
            srv = self.services.get(srv_name)
        if srv is None:
            diag_warn("service not found: " + srv_name +
                      ", please start the service")
            return Response(
                ResStatus.eNo,
                "service not found: " + srv_name
            )
        return srv.invoke(func_name, pkg)

    def async_request(self, url, pkg=None, callback=None):
        srv_name, func_name, err_msg = self._parse_request_url(url)
        if err_msg:
            diag_warn(err_msg)
            return

        # 服务器已关闭直接拒绝，池已停止时 submit 同样异常兜底
        if self.shutdown_done:
            diag_error("server is shutting down, async request ignored")
            return

        def task():
            try:
                # 执行时才查表；
                # 拼回完整 URL 走 request 的解析与查表路径
                result = self.request(srv_name + func_name, pkg)
                if callback:
                    callback(result)
            except Exception as e:
                diag_error("async task exception caught: " + str(e))

        try:
            self.executor.submit(task)
        except RuntimeError:
            diag_error("server is shutting down, async request ignored")