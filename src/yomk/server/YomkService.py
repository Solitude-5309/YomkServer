import weakref
from typing import TYPE_CHECKING, Any, Callable, Optional

from readerwriterlock import rwlock

from yomk.msg.ctx import CheckStatus
from yomk.msg.fn_pool import FuncInfo
from yomk.msg.server import ResStatus, Response
from yomk._diag import error as diag_error
from yomk._diag import info as diag_info
from yomk._diag import warn as diag_warn

if TYPE_CHECKING:
    from yomk.server.YomkServer import YomkServer


class YomkService:
    def __init__(self, server: "YomkServer"):
        self.name: str = ""
        # 弱引用持有 server（对齐 C++ weak_ptr 模型）：
        # 服务存活不阻碍 server 销毁，销毁后经判活走 "server has been destroyed" 契约；
        # 经 property 解引用，属性名与读写语义保持不变
        self._server_ref = weakref.ref(server) if server is not None else None
        self.functions: dict[str, Callable[[Any], Response]] = {}
        self.function_infos: dict[str, FuncInfo] = {}
        self.registered: bool = False
        # 注销标志：del_service/同名替换置位，
        # shutdown 不置位——删除即停（weak_func 判活叠加此标志），排空期回调照常执行
        self.m_deleted: bool = False
        self.rwlock_functions = rwlock.RWLockFair()
        if not server:
            diag_error("server is null")

    @property
    def server(self) -> Optional["YomkServer"]:
        # 弱引用解引用：server 已销毁返回 None（对齐 C++ weak_ptr 判活）
        return self._server_ref() if self._server_ref is not None else None

    @server.setter
    def server(self, value: Optional["YomkServer"]) -> None:
        # 兼容既有赋值用法（如 YomkServerInfo 置 None 解除强引用）
        self._server_ref = weakref.ref(value) if value is not None else None

    def get_name(self):
        return self.name

    def set_name(self, name):
        if not name:
            diag_warn("name is empty, set name failed, use default name")
            return
        if self.registered:
            diag_warn("service rename refused, already registered, refused name '" + name + "'")
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
            diag_warn("function name parse error: " +
                      name + ", please start with /")
            return

        if not function:
            diag_warn("function is null, install function refused: " + name)
            return

        with self.rwlock_functions.gen_wlock():
            if (name in self.functions):
                # 文档化热替换：级别 INFO（对齐 C++）
                diag_info(
                    f"function '{name}' already exists, replace with current function")
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
            # 字典序遍历（对齐 C++ std::map 迭代序）
            for func_name in sorted(self.functions):
                info = FuncInfo(func_name)
                stored = self.function_infos.get(func_name)
                if stored:
                    info.msg_name = stored.msg_name
                    info.description = stored.description
                infos[func_name] = info
        return infos

    def invoke(self, name, pkg):
        # 失败路径不打 diag：Response 已携带同信息（对齐 C++ 双重报警删除）
        if not name or not name.startswith("/"):
            return Response(
                ResStatus.eNo,
                "function name parse error: " +
                str(name) + ", please start with /"
            )
        with self.rwlock_functions.gen_rlock():
            if (name not in self.functions):
                return Response(ResStatus.eNo, "function not found: " + name)
            func = self.functions[name]
        return func(pkg)

    def request(self, url, pkg=None):
        # 构造时 server 为空（或被置 None）→ "service is null"；
        # server 已销毁（弱引用失效）→ "server has been destroyed"（对齐 C++ 契约）
        if self._server_ref is None:
            diag_error("service is null, please check service")
            return Response(ResStatus.eNo, "service is null")
        srv = self._server_ref()
        if srv is None:
            diag_error("server has been destroyed, request ignored")
            return Response(ResStatus.eNo, "server has been destroyed")
        return srv.request(url, pkg)

    def async_request(self, url, pkg=None, callback=None):
        if self._server_ref is None:
            diag_error("service is null, please check service")
            return
        srv = self._server_ref()
        if srv is None:
            diag_error("server has been destroyed, async request ignored")
            return
        srv.async_request(url, pkg, callback)


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