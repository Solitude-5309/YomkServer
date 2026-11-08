import weakref
from typing import TYPE_CHECKING, Any, Callable

from readerwriterlock import rwlock

from yomk.msg.ctx import CheckStatus
from yomk.msg.fn_pool import FuncInfo
from yomk.msg.server import ResStatus, Response
from yomk.server._diag import error as diag_error

if TYPE_CHECKING:
    from yomk.server.YomkServer import YomkServer


class YomkService:
    def __init__(self, server: "YomkServer"):
        self.name: str = ""
        self.server: "YomkServer" = server
        self.functions: dict[str, Callable[[Any], Response]] = {}
        self.function_infos: dict[str, FuncInfo] = {}
        self.registered: bool = False
        # 注销标志：del_service/同名替换置位，
        # shutdown 不置位——删除即停（weak_func 判活叠加此标志），排空期回调照常执行
        self.m_deleted: bool = False
        self.rwlock_functions = rwlock.RWLockFair()
        if not server:
            diag_error("server is null")

    def get_name(self):
        return self.name

    def set_name(self, name):
        if not name:
            diag_error("name is empty, set name failed, use default name")
            return
        if self.registered:
            diag_error("service is registered, rename is not allowed -> " + name)
            return
        self.name = name

    def mark_registered(self):
        # 框架内部使用：服务入表注册后锁定服务名，此后 set_name 改名被拒绝
        self.registered = True

    def mark_deleted(self):
        # 框架内部使用：del_service/同名替换置位注销标志，weak_func 判活叠加此标志
        # （删除即停：即使回调仍被在途任务持有，调用也立即丢弃）
        self.m_deleted = True

    def deleted(self):
        # 注销标志查询
        return self.m_deleted

    def weak_func(self, func, discard="response"):
        # 包装回调为弱绑定判活回调：weakref(self) 引用计数层
        # + deleted() 注销标志层，双层独立生效；丢弃时按 discard 声明给出契约返回值
        # （"response" 返 eNo+契约消息 / "check" 放行 eAccept / "none" 返 None——
        # 动态类型下由调用方声明丢弃形态）
        # 绑定方法持实例强引用，直接持有会抵消判活：经 WeakMethod 弱化宿主；
        # 拆分两 helper 是刻意的——绑定方法不得被
        # 本函数闭包捕获（cython 函数级 scope 会持 func 参数），仅裸函数参与捕获
        if getattr(func, "__self__", None) is not None:
            return self._weak_meth(func, discard)
        return self._weak_plain(func, discard)

    def _weak_meth(self, func, discard):
        # 绑定方法分支：WeakMethod 弱化宿主，判活叠加方法宿主存活；
        # 本分支闭包仅持有 WeakMethod（内部全弱引用），不持绑定方法
        self_ref = weakref.ref(self)
        meth_ref = weakref.WeakMethod(func)

        def wrapper(*args, **kwargs):
            srv = self_ref()
            live_func = meth_ref()
            if srv is None or live_func is None or srv.m_deleted:
                return _yomk_weak_discard_result(discard)
            return live_func(*args, **kwargs)

        return wrapper

    def _weak_plain(self, func, discard):
        # 普通函数/闭包分支：不持实例引用，判活仅由 self_ref 承载
        self_ref = weakref.ref(self)

        def wrapper(*args, **kwargs):
            srv = self_ref()
            if srv is None or srv.m_deleted:
                return _yomk_weak_discard_result(discard)
            return func(*args, **kwargs)

        return wrapper

    def init(self):
        pass

    def deinit(self):
        pass

    def install_func(self, name, function, msg_name="", description=""):
        # msg_name 为期望消息类型元数据，description 为使用者自定义函数说明，均仅供内省
        if not name.startswith("/"):
            diag_error("install function name is error: " +
                       name + ", please start with /")
            return

        if not function:
            diag_error("install function is empty")
            return

        with self.rwlock_functions.gen_wlock():
            if (name in self.functions):
                diag_error(
                    f"install function already exists -> {name}, update to current function")
            self.functions[name] = self.weak_func(function)
            if msg_name or description:
                self.function_infos[name] = FuncInfo(
                    name, msg_name, description)
            else:
                # 两参覆盖安装时清除残留的函数信息，保持内省与注册一致
                self.function_infos.pop(name, None)

    def func_infos(self):
        infos: dict[str, FuncInfo] = {}
        with self.rwlock_functions.gen_rlock():
            for func_name in self.functions:
                info = FuncInfo(func_name)
                stored = self.function_infos.get(func_name)
                if stored:
                    info.msg_name = stored.msg_name
                    info.description = stored.description
                infos[func_name] = info
        return infos

    def invoke(self, name, pkg):
        if not name or not name.startswith("/"):
            diag_error("function name parse error: " + str(name) +
                       ", please start with /")
            return Response(
                ResStatus.eNo,
                "function name parse error: " +
                str(name) + ", please start with /"
            )
        with self.rwlock_functions.gen_rlock():
            if (name not in self.functions):
                diag_error(
                    f"function not found -> {name}, please use YomkInstallFunc to install this function.")
                return Response(ResStatus.eNo, "function not found: " + name)
            func = self.functions[name]
        return func(pkg)

    def request(self, url, pkg=None):
        if not self.server:
            diag_error("service is null, please check service")
            return Response(ResStatus.eNo, "service is null")
        return self.server.request(url, pkg)

    def async_request(self, url, pkg=None, callback=None):
        if not self.server:
            diag_error("service is null, please check service")
            return
        self.server.async_request(url, pkg, callback)


def _yomk_weak_discard_result(discard):
    # weak_func 丢弃契约：
    # "response" 返 eNo+契约消息 / "check" 放行 eAccept / "none" 返 None
    diag_error("service has been deleted or unregistered, callback ignored.")
    if discard == "response":
        return Response(
            ResStatus.eNo,
            "service has been deleted or unregistered, callback ignored.")
    if discard == "check":
        return CheckStatus.eAccept
    return None