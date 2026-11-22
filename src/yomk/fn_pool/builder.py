"""Function pool domain chained builder: registration, invocation and introspection.

函数池域链式 Builder：注册、调用与内省。
"""

from yomk.msgs.fn_pool import CallFunction, FuncInfoList, Function
from yomk.msgs.server import ResStatus
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import require_msg_payload, unpack
from yomk.server import _facade
from yomk.server.YomkService import _yomk_weak_slot_fill
from yomk.url.fn_pool import (FP_ALL, FP_CALL, FP_NAME, FP_NAMES,
                              FP_REGISTER, FP_UNREGISTER)


class FnPoolBuilder:
    """Function pool domain builder: registration, invocation and introspection.

    函数池域链式 Builder：注册、调用与内省。

    Usage:
        Register:   fn_pool().fn_name(n).fn(f).msg_name(m).description(d).install()
        Unregister: fn_pool().fn_name(n).uninstall()
        Call:       fn_pool().fn_name(n).msg(data).call()
        Query:      list() / fn_name(n).get_info() / all()

        install/uninstall 返回三态 ResStatus；call 阻塞执行并返回 Response；
        内省终结返回 Response，list()/all() 的 data 为裸 list。
    """

    def __init__(self):
        self._name = ""
        self._fn = None
        self._msg_name = ""
        self._description = ""
        self._pkg = None

    def fn_name(self, n):
        self._name = n
        return self

    def fn(self, f):
        # 槽期望 Response；缺省按 "response" 填充，显式误配在注册期诊断
        self._fn = _yomk_weak_slot_fill(f, "fn_pool", "response")
        return self

    def msg_name(self, m):
        self._msg_name = m
        return self

    def description(self, d):
        self._description = d
        return self

    def msg(self, data):
        require_msg_payload(data, "msg()")
        self._pkg = data
        return self

    def install(self):
        """Register the accumulated function; returns a tri-state status.

        注册累积的函数；名称为空或同名已存在时失败，返回三态 ResStatus。
        """
        return _facade.chain_status(_facade.api_request(
            FP_REGISTER,
            Function(self._name, self._fn, self._msg_name, self._description)))

    def uninstall(self):
        """Unregister a function by name; returns a tri-state status.

        按名称注销函数；函数不存在时失败，返回三态 ResStatus。
        """
        return _facade.chain_status(
            _facade.api_request(FP_UNREGISTER, String(self._name)))

    def call(self):
        """Call the registered function synchronously.

        阻塞调用已注册函数，返回携带函数返回值的 Response；函数不存在返回 No。
        """
        return _facade.api_request(FP_CALL, CallFunction(self._name, self._pkg))

    def list(self):
        """Return all registered function names as a plain list.

        返回全部已注册函数名（裸 list）；未初始化返回 Invalid。
        """
        res = _facade.api_request(FP_NAMES, None)
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, StringArray).items
        return res

    def get_info(self):
        """Return introspection metadata of the target function.

        返回指定函数的内省元信息；函数不存在返回 No。
        """
        return _facade.api_request(FP_NAME, String(self._name))

    def all(self):
        """Return introspection metadata of all functions as a plain list.

        返回全部已注册函数的内省元信息（裸 list）；未初始化返回 Invalid。
        """
        res = _facade.api_request(FP_ALL, None)
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, FuncInfoList).items
        return res


def fn_pool():
    """Return the function pool domain chained entry.

    返回函数池域链式入口。
    """
    return FnPoolBuilder()