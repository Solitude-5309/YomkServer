from abc import ABC, abstractmethod

from yomk.server import _facade
from yomk.server._diag import error as diag_error


class YomkBoot(ABC):
    # 启动编排钩子：before -> start -> after，未显式返回等价成功
    def __init__(self) -> None:
        pass

    @abstractmethod
    def before(self) -> int:
        pass

    @abstractmethod
    def start(self) -> int:
        pass

    @abstractmethod
    def after(self) -> int:
        pass


class AsyncRequestBuilder:
    # 异步分面：server 域 .async_(cb) 之后继续 .msg() 并以 .request() 无返回终结
    def __init__(self, url, callback):
        self._url = url
        self._callback = callback
        self._msg = None

    def msg(self, m):
        self._msg = m
        return self

    def request(self):
        # 异步终结无返回；未初始化记诊断
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not init")
            return None
        return server.async_request(self._url, self._msg, self._callback)


class ServerBuilder:
    # server 请求域 builder：url/msg 累积 + request()/status() 收敛 + 服务编排终结
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
        self._msg = m
        return self

    def request(self):
        # 同步请求终结，返回 Response
        return _facade.api_request(self._url, self._msg)

    def status(self):
        # 写终结三态收敛（失败记诊断），返回 ResStatus
        return _facade.chain_status(self.request())

    def async_(self, func=None):
        # 切入异步分面：已累积消息携带进分面
        ab = AsyncRequestBuilder(self._url, func)
        ab._msg = self._msg
        return ab

    def version(self):
        # 框架版本号（server 域版本）
        from yomk.server import __version__
        return __version__

    def async_thread(self, n):
        # 累积异步线程池大小（init 用）
        self._async_thread_count = n
        return self

    def init(self):
        # 初始化框架单例（进程级幂等，shutdown 后不再重建）
        _facade.init_locked(self._async_thread_count)

    def instance(self):
        # 全局单例（未初始化 None）
        return _facade.current_server()

    def shutdown(self):
        # 关闭服务器并释放所有服务
        _facade.shutdown_locked()

    def srv(self, cls, name=""):
        # 累积待创建服务类与名（create 用）
        self._srv_cls = cls
        self._srv_name = name
        return self

    def create(self):
        # 创建并注册服务，返回 0/-1
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not init")
            return -1
        if self._srv_cls is None:
            # 未累积服务类守卫（诊断走 diag 通道）
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
        # 注册已构造服务，返回 0/-1
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not init")
            return -1
        if not self._srv:
            diag_error("YomkService is null")
            return -1
        if self._srv_name:
            self._srv.set_name(self._srv_name)
        return server.add_service(self._srv)

    def remove(self):
        # 按名称注销服务，返回 0/-1
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not init")
            return -1
        if not self._srv_name:
            diag_error("service name is empty")
            return -1
        return server.del_service(self._srv_name)

    def boot_ptr(self, b):
        self._boot = b
        return self

    def run(self):
        # 执行启动编排（before -> start -> after），返回 0/钩子错误码
        return _facade.boot_locked(self._boot)


def server():
    # server 域链式入口
    return ServerBuilder()