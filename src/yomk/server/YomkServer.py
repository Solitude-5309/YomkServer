"""Server instance: service registry and request routing.

服务器实例：服务注册表与请求路由。
"""

import os
from concurrent.futures import ThreadPoolExecutor

from readerwriterlock import rwlock

from yomk.msgs.server import ResStatus, Response
from yomk.msgs.YomkMsg import require_msg_instance
from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn
from yomk.server.YomkService import YomkService


class YomkServer:
    """Server instance holding the service registry and async request pool.

    服务器实例：持有服务注册表与异步请求池。

    Note:
        Registry access is guarded by a fair RW lock; service deinit always runs
        outside the lock to avoid re-entrant deadlock.
        注册表由公平读写锁保护；服务 deinit 一律在锁外执行，避免重入死锁。
    """

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
        """Create a server instance; 0 picks the pool size from the CPU count.

        创建服务器实例；async_thread_count 为异步线程池大小，0 表示自动检测。
        """
        return cls(async_thread_count)

    def start_service(self, srv_names):
        """Start the named built-in services; unknown names warn and are skipped.

        启动指定内置服务；未知名 WARN 后跳过，恒返回 0。
        """
        # 延迟导入：避免 server 在模块级反向依赖各扩展域
        from yomk.ctx.YomkContext import YomkContext
        from yomk.event_loop.YomkEventLoop import YomkEventLoop
        from yomk.fn_pool.YomkFunctionPool import YomkFunctionPool
        from yomk.log.YomkLogger import YomkLogger
        from yomk.srv_info.YomkServerInfo import YomkServerInfo

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
        """Instantiate and register a service of the given class.

        创建并注册指定类型的服务，返回 0 成功 / -1 失败。
        """
        srv = srv_cls(self)
        if srv_name:
            srv.set_name(srv_name)
        return self.add_service(srv)

    def add_service(self, service):
        """Register a service and run its init(); 0 on success, -1 on failure.

        注册服务并执行 init()；同名覆盖，init 失败或抛异常时回滚并返回 -1。
        """
        if not isinstance(service, YomkService):
            diag_warn("service is null, please check the service")
            return -1

        old_srv = None
        with self.rwlock_services.gen_wlock():
            if (service.get_name() in self.services):
                diag_warn(
                    f"service '{service.get_name()}' already exists, replace with current service")
            old_srv = self.services.get(service.get_name())
            self.services[service.get_name()] = service
            # 入表即锁定服务名，保证服务名与 service map 键一致
            service.mark_registered()

        # 同名替换：锁外置位旧服务并 deinit，置位后其弱绑定回调立即丢弃
        if old_srv is not None:
            old_srv.mark_deleted()
            old_srv.deinit()

        # init 抛异常视同失败；返回 None 等价成功，非零统一回滚
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
        """Remove a service by name; 0 on success, -1 if absent.

        按名称移除服务并在锁外 deinit；服务不存在返回 -1。
        """
        srv = None
        with self.rwlock_services.gen_wlock():
            if (srv_name not in self.services):
                diag_warn("service not found: " + srv_name + ", delete ignored")
                return -1
            srv = self.services.pop(srv_name)

        # 锁外清理：置位后弱绑定回调立即丢弃（删除即停）
        if srv is not None:
            srv.mark_deleted()
            srv.deinit()
        return 0

    def shutdown(self):
        """Shut down the server; repeated calls are no-ops.

        停止异步池、清空服务表并在锁外逐个 deinit；重复调用直接返回。
        """
        if self.shutdown_done:
            return
        self.shutdown_done = True

        self.executor.shutdown(wait=True)

        with self.rwlock_services.gen_wlock():
            srvs = list(self.services.values())
            self.services.clear()

        for srv in srvs:
            srv.deinit()

    def service_names(self):
        """Return all service names in sorted order.

        按字典序返回全部服务名。
        """
        with self.rwlock_services.gen_rlock():
            return sorted(self.services.keys())

    def service_func_infos(self, srv_name):
        """Return a function info snapshot of the named service.

        返回指定服务的函数元信息快照；服务不存在返回空 dict。
        """
        with self.rwlock_services.gen_rlock():
            srv = self.services.get(srv_name)
        if srv is None:
            diag_warn("service not found: " + srv_name +
                      ", please start the service")
            return {}
        return srv.func_infos()

    def _parse_request_url(self, url):
        # 拆 /ServiceName/func_name；失败返回 err_msg，由调用方决定响应或丢弃
        if not url or url[0] != "/":
            return "", "", "url parse error: " + str(url) + ", please start with /"
        pos_end = url.find("/", 1)
        if pos_end == -1:
            return "", "", "url parse error: " + url + ", no service name"
        srv_name = url[:pos_end]
        if not srv_name:
            return "", "", "url parse error: " + url + ", srv name is empty"
        func_name = url[pos_end:]
        if not func_name:
            return "", "", "url parse error: " + url + ", function name is empty"
        return srv_name, func_name, None

    def request(self, url, msg=None):
        """Route a request to the target service.

        按 URL 路由到目标服务；解析失败或服务不存在返回 No。
        载荷必须是消息实例或 None；None（空载荷）由调用方先行判断，非实例当场 TypeError。
        """
        if msg is not None:
            require_msg_instance(msg, "request()")
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
        return srv.invoke(func_name, msg)

    def async_request(self, url, msg=None, callback=None):
        """Submit a request to the async pool; the result goes to callback.

        异步提交请求，结果经 callback 回调，无返回值；解析失败、已关闭或池已停止时
        记诊断并丢弃。载荷必须是消息实例或 None；None（空载荷）由调用方先行判断，
        非实例当场 TypeError。
        """
        if msg is not None:
            require_msg_instance(msg, "async_request()")
        srv_name, func_name, err_msg = self._parse_request_url(url)
        if err_msg:
            diag_warn(err_msg)
            return

        # 已关闭直接拒绝；池已停止时 submit 也会抛 RuntimeError
        if self.shutdown_done:
            diag_error("server is shutting down, async request ignored")
            return

        def task():
            try:
                # 执行时才查表；拼回完整 URL 复用 request 的解析与路由
                result = self.request(srv_name + func_name, msg)
                if callback:
                    callback(result)
            except Exception as e:
                diag_error("async task exception caught: " + str(e))

        try:
            self.executor.submit(task)
        except RuntimeError:
            diag_error("server is shutting down, async request ignored")