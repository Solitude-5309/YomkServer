"""Service base class: function registration and inter-service requests.

服务基类：函数注册、调用与跨服务请求。
"""

import weakref
from typing import TYPE_CHECKING, Any, Callable, Optional

from readerwriterlock import rwlock

from yomk.msgs.ctx import CheckStatus
from yomk.msgs.fn_pool import FuncInfo
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.YomkMsg import require_msg_payload
from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn

if TYPE_CHECKING:
    from yomk.server.YomkServer import YomkServer


class YomkService:
    """Service base class: function registration and inter-service requests.

    服务基类：函数注册、调用与跨服务请求。

    Note:
        The server is held weakly; after server destruction requests return No
        and weak-bound callbacks are discarded by their declared contract.
        弱引用持有 server；销毁后请求返回 No，弱绑定回调按 discard 声明丢弃。
    """

    def __init__(self, server: "YomkServer"):
        self.name: str = ""
        # 弱引用持有 server：服务存活不阻碍 server 销毁，销毁后经判活返回契约错误
        self._server_ref = weakref.ref(server) if server is not None else None
        self.functions: dict[str, Callable[[Any], Response]] = {}
        self.function_infos: dict[str, FuncInfo] = {}
        self.registered: bool = False
        # 注销标志：del_service/同名替换置位；shutdown 排空路径不置位，排空期回调照常执行
        self.m_deleted: bool = False
        self.rwlock_functions = rwlock.RWLockFair()
        if not server:
            diag_error("server is null")

    @property
    def server(self) -> Optional["YomkServer"]:
        # 弱引用解引用：server 已销毁返回 None
        return self._server_ref() if self._server_ref is not None else None

    @server.setter
    def server(self, value: Optional["YomkServer"]) -> None:
        # 兼容赋值用法（如 YomkServerInfo 置 None 解除强引用）
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
        # 框架内部使用：注册后锁定服务名，set_name 改名被拒绝
        self.registered = True

    def mark_deleted(self):
        # 框架内部使用：置注销标志并补 WARN，此后弱绑定调用走丢弃路径（ERROR）
        self.m_deleted = True
        diag_warn("service '" + self.name
                  + "' deleted, weak-bound callbacks discarded from now on")

    def deleted(self):
        return self.m_deleted

    def weak_func(self, func, discard=None):
        """Wrap a callback so it stops firing when this service or its server dies.

        将回调包装为弱绑定回调：服务销毁或注销后不再触发。

        Args:
            func: Plain function, closure or bound method. 普通函数、闭包或绑定方法。
            discard: Result returned after discard: "response" (No response),
                "check" (eAccept) or "none" (None); None lets the consuming slot
                fill its default.
                丢弃时的契约返回值；None 由消费槽位按 API 形态填充缺省。
        """
        # 双层判活：weakref(self) 与注销标志独立生效
        # 绑定方法经 WeakMethod 弱化宿主，且不得被本函数闭包捕获（Cython 函数级 scope）
        if getattr(func, "__self__", None) is not None:
            return self._weak_meth(func, discard)
        return self._weak_plain(func, discard)

    def _weak_meth(self, func, discard):
        # 绑定方法分支：闭包只持 WeakMethod（内部全弱引用），不持绑定方法
        self_ref = weakref.ref(self)
        meth_ref = weakref.WeakMethod(func)

        def wrapper(*args, **kwargs):
            srv = self_ref()
            live_func = meth_ref()
            if srv is None or live_func is None or srv.m_deleted:
                return _yomk_weak_discard_result(wrapper.__yomk_discard__
                                                 if wrapper.__yomk_discard__ is not None
                                                 else "response")
            return live_func(*args, **kwargs)

        wrapper.__yomk_discard__ = discard
        wrapper.__yomk_discard_explicit__ = discard is not None
        return wrapper

    def _weak_plain(self, func, discard):
        # 普通函数/闭包分支：不持实例引用，判活只由 self_ref 承载
        self_ref = weakref.ref(self)

        def wrapper(*args, **kwargs):
            srv = self_ref()
            if srv is None or srv.m_deleted:
                return _yomk_weak_discard_result(wrapper.__yomk_discard__
                                                 if wrapper.__yomk_discard__ is not None
                                                 else "response")
            return func(*args, **kwargs)

        wrapper.__yomk_discard__ = discard
        wrapper.__yomk_discard_explicit__ = discard is not None
        return wrapper

    def init(self):
        pass

    def deinit(self):
        pass

    def install_func(self, name, function, msg_name="", description=""):
        """Register a function under a slash-prefixed name.

        以 "/" 开头的名称注册函数；重复注册直接覆盖既有注册。

        Args:
            msg_name: Expected message type name; pass ``MsgClass.TYPE_NAME`` instead
                of a literal so the value follows renames.
                期望消息类型名，一律传消息类的 TYPE_NAME，勿手写字面量。
            description: Free-form description for introspection only.
                仅供内省的自定义说明。
        """
        if not name.startswith("/"):
            diag_warn("function name parse error: " +
                      name + ", please start with /")
            return

        if not function:
            diag_warn("function is null, install function refused: " + name)
            return

        with self.rwlock_functions.gen_wlock():
            if (name in self.functions):
                diag_warn(
                    f"function '{name}' already exists, replace with current function")
            self.functions[name] = self.weak_func(function)
            if msg_name or description:
                self.function_infos[name] = FuncInfo(
                    name, msg_name, description)
            else:
                # 不传元数据时清除残留，保持内省与注册一致
                self.function_infos.pop(name, None)

    def func_infos(self):
        """Return a snapshot of registered functions in name order.

        返回按名称排序的函数元信息快照。
        """
        infos: dict[str, FuncInfo] = {}
        with self.rwlock_functions.gen_rlock():
            for func_name in sorted(self.functions):
                info = FuncInfo(func_name)
                stored = self.function_infos.get(func_name)
                if stored:
                    info.msg_name = stored.msg_name
                    info.description = stored.description
                infos[func_name] = info
        return infos

    def invoke(self, name, msg):
        """Invoke a registered function; failures are reported only via Response.

        调用已注册函数；失败只经 Response 返回，不重复记诊断。
        """
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
        return func(msg)

    def request(self, url, msg=None):
        """Send a request through the owning server.

        经所属 server 发起请求；服务无 server 或 server 已销毁返回 No。
        载荷必须是消息实例或 None，否则入口当场 TypeError。
        """
        require_msg_payload(msg, "request()")
        if self._server_ref is None:
            diag_error("service is null, please check service")
            return Response(ResStatus.eNo, "service is null")
        srv = self._server_ref()
        if srv is None:
            diag_error("server has been destroyed, request ignored")
            return Response(ResStatus.eNo, "server has been destroyed")
        return srv.request(url, msg)

    def async_request(self, url, msg=None, callback=None):
        """Send a request asynchronously; failures are diagnostic-only.

        异步发起请求，无返回值；服务无 server 或 server 已销毁时记诊断并丢弃。
        载荷必须是消息实例或 None，否则入口当场 TypeError。
        """
        require_msg_payload(msg, "async_request()")
        if self._server_ref is None:
            diag_error("service is null, please check service")
            return
        srv = self._server_ref()
        if srv is None:
            diag_error("server has been destroyed, async request ignored")
            return
        srv.async_request(url, msg, callback)


def _yomk_weak_slot_fill(cb, slot, expected):
    # 槽位缺省填充：未显式声明 discard 的弱绑定回调按槽位期望填充；
    # 显式声明不符则注册期诊断；裸回调（无显式标记）原样透传
    if getattr(cb, "__yomk_discard_explicit__", None) is None:
        return cb
    if not cb.__yomk_discard_explicit__:
        cb.__yomk_discard__ = expected
        return cb
    d = getattr(cb, "__yomk_discard__", None)
    if d != expected:
        diag_error("weak_func discard '" + d + "' mismatches " + slot
                   + " slot (expects '" + expected
                   + "'), discarded callback will return wrong type")
    return cb


def _yomk_weak_discard_result(discard):
    # 丢弃契约："response" 返 eNo Response / "check" 放行 / "none" 返 None
    diag_error("service has been deleted or unregistered, callback ignored.")
    if discard == "response":
        return Response(
            ResStatus.eNo,
            "service has been deleted or unregistered, callback ignored.")
    if discard == "check":
        return CheckStatus.eAccept
    return None