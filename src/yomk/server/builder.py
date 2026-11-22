"""Server domain chained builder: requests, lifecycle and service management.

服务器域链式 Builder：请求、生命周期与服务管理。
"""

from yomk.server import _facade
from yomk._diag import error as diag_error
from yomk.msgs.YomkMsg import require_msg


class AsyncRequestBuilder:
    """Asynchronous request facet entered by server().async_(cb).

    异步请求分面：async_(cb) 之后 msg() 累积、request() 投递后立即返回。

    Note:
        request() returns None; an uninitialized server logs a diagnostic and drops the request.
        无返回值；未初始化时记诊断并丢弃请求。
    """

    def __init__(self, url, callback):
        self._url = url
        self._callback = callback
        self._msg = None

    def msg(self, m):
        require_msg(m, "msg()")
        self._msg = m
        return self

    def request(self):
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return None
        return server.async_request(self._url, self._msg, self._callback)


class ServerBuilder:
    """Server domain builder: request, lifecycle and service management.

    服务器域链式 Builder：请求、生命周期与服务管理。

    Usage:
        Request:   server().url(u).msg(m).request() / .status()
        Async:     server().url(u).async_(cb).msg(m).request()
        Lifecycle: server().async_thread(n).init() / .instance() / .shutdown()
        Service:   server().srv(cls, name).create() / .srv_ptr(s).add() / .srv_name(n).remove()
        Boot:      server().boot_ptr(b).run()

        request 返回 Response，status 仅返回三态 ResStatus；异步投递后立即返回；
        create/add/remove 返回 0 成功 / -1 失败；run 返回 0 或钩子错误码。
    """

    def __init__(self):
        self._url = ""
        self._msg = None
        self._async_thread_count = 0
        self._srv_cls = None
        self._srv_name = ""
        self._srv = None
        self._boot = None

    def url(self, u):
        self._url = u
        return self

    def msg(self, m):
        require_msg(m, "msg()")
        self._msg = m
        return self

    def request(self):
        """Send the request synchronously and return the Response.

        同步请求终结，返回 Response；未初始化返回 Invalid。
        """
        return _facade.api_request(self._url, self._msg)

    def status(self):
        """Send the request and collapse the result to a tri-state status.

        同步请求并按三态收敛，失败记诊断。
        """
        return _facade.chain_status(self.request())

    def async_(self, func=None):
        """Switch to the asynchronous request facet, carrying the accumulated message.

        切入异步分面，已累积的 msg 随之携带。
        """
        ab = AsyncRequestBuilder(self._url, func)
        ab._msg = self._msg
        return ab

    def version(self):
        """Return the framework version of the server domain.

        返回 server 域框架版本号。
        """
        from yomk.server import __version__
        return __version__

    def async_thread(self, n):
        """Accumulate the async thread pool size used by init().

        累积异步线程池大小，供 init() 使用。
        """
        self._async_thread_count = n
        return self

    def init(self):
        """Initialize the process-wide server singleton; repeated calls are idempotent.

        初始化进程级单例；重复调用幂等，shutdown 后不再重建。
        """
        _facade.init_locked(self._async_thread_count)

    def instance(self):
        """Return the current server singleton, or None before init().

        返回全局单例；未初始化返回 None。
        """
        return _facade.current_server()

    def shutdown(self):
        """Shut down the server and release all services.

        关闭服务器并释放全部服务；重复调用安全。
        """
        _facade.shutdown_locked()

    def srv(self, cls, name=""):
        # 累积待创建服务类与名（create 用）
        self._srv_cls = cls
        self._srv_name = name
        return self

    def create(self):
        """Create and register the accumulated service; 0 on success, -1 on failure.

        创建并注册累积的服务；未初始化或未调用 srv() 返回 -1。
        """
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return -1
        if self._srv_cls is None:
            diag_error("service type is not set (call srv() first)")
            return -1
        return server.new_service(self._srv_cls, self._srv_name)

    def srv_name(self, n):
        self._srv_name = n
        return self

    def srv_ptr(self, s):
        self._srv = s
        return self

    def add(self):
        """Register an already-constructed service; 0 on success, -1 on failure.

        注册已构造的服务；未初始化、服务为空或同名初始化失败返回 -1。
        """
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return -1
        if not self._srv:
            diag_error("YomkService is null")
            return -1
        if self._srv_name:
            self._srv.set_name(self._srv_name)
        return server.add_service(self._srv)

    def remove(self):
        """Unregister a service by name; 0 on success, -1 on failure.

        按名称注销服务；未初始化或名称为空返回 -1。
        """
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return -1
        if not self._srv_name:
            diag_error("service name is empty")
            return -1
        return server.del_service(self._srv_name)

    def boot_ptr(self, b):
        self._boot = b
        return self

    def run(self):
        """Run the boot orchestration (before -> start -> after).

        执行启动编排；返回 0 或首个非零钩子错误码，异步线程数沿用 async_thread() 累积值。
        """
        return _facade.boot_locked(self._boot, self._async_thread_count)


def server():
    """Return the server domain chained entry.

    返回 server 域链式入口。
    """
    return ServerBuilder()