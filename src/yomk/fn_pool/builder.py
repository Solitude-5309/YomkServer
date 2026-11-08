from yomk.msg.fn_pool import CallFunction, Function
from yomk.server import _facade


class FnPoolBuilder:
    # function_pool 域 builder：fn_name/fn/msg_name/description/msg 累积 + 终结
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
        self._fn = f
        return self

    def msg_name(self, m):
        self._msg_name = m
        return self

    def description(self, d):
        self._description = d
        return self

    def msg(self, data):
        self._pkg = data
        return self

    def install(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkFunctionPool/register",
            Function(self._name, self._fn, self._msg_name, self._description)))

    def uninstall(self):
        return _facade.chain_status(
            _facade.api_request("/YomkFunctionPool/unregister", self._name))

    def call(self):
        # 阻塞调用已注册函数
        return _facade.api_request(
            "/YomkFunctionPool/call", CallFunction(self._name, self._pkg))

    def list(self):
        return _facade.api_request("/YomkFunctionPool/names", None)

    def get_info(self):
        # 查询单个已注册函数元信息（用 fn_name 累积）
        return _facade.api_request("/YomkFunctionPool/name", self._name)

    def all(self):
        return _facade.api_request("/YomkFunctionPool/all", None)


def fn_pool():
    # function_pool 域链式入口
    return FnPoolBuilder()