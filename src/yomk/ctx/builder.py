from yomk.msg.ctx import (
    Context,
    ContextChecker,
    ContextGroupKey,
    ContextMonitor,
)
from yomk.msg.server import ResStatus
from yomk.server import _facade
from yomk.server._diag import error as diag_error


class GroupSwitch:
    # 组级开关分面：ctx().group(g).checker()/monitor() 之后仅 enable()/disable()
    def __init__(self, group, monitor):
        self._group = group
        self._monitor = monitor  # False = 校验门控，True = 变更监听

    def enable(self):
        url = "/YomkContext/turn_on_monitor" if self._monitor else "/YomkContext/turn_on_checker"
        return _facade.chain_status(_facade.api_request(url, self._group))

    def disable(self):
        url = "/YomkContext/turn_off_monitor" if self._monitor else "/YomkContext/turn_off_checker"
        return _facade.chain_status(_facade.api_request(url, self._group))


class ContextValueBuilder:
    # 读路径分面：ctx().key(n).group(g).msg() 之后 with_default(d).get() 直取值本体
    def __init__(self, key, group):
        self._key = key
        self._group = group
        self._default = None

    def with_default(self, d):
        self._default = d
        return self

    def get(self):
        # 未命中/失败回退 default（未初始化同）
        if _facade.current_server() is None:
            return self._default
        res = _facade.api_request(
            "/YomkContext/get", Context(self._key, self._default, self._group))
        if res.status == ResStatus.eOk:
            return res.data
        diag_error("get context failed")
        return self._default


class ContextBuilder:
    # context 域 builder：key/group/value 累积 + 写终结三态收敛 + 读终结 Response
    def __init__(self):
        self._key = ""
        self._group = "main"
        self._value = None
        self._cb_kind = None  # "checker" / "monitor" / None（后调覆盖先调）
        self._checker = None
        self._monitor = None
        self._async_monitor = False

    def key(self, n):
        self._key = n
        return self

    def group(self, g="main"):
        self._group = g
        return self

    def value(self, v):
        self._value = v
        return self

    def create(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkContext/create", Context(self._key, self._value, self._group)))

    def set(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkContext/set", Context(self._key, self._value, self._group)))

    def destroy(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkContext/destroy", ContextGroupKey(self._group, self._key)))

    def checker_fn(self, fn):
        self._checker = fn
        self._cb_kind = "checker"
        return self

    def monitor_fn(self, fn):
        self._monitor = fn
        self._cb_kind = "monitor"
        return self

    def async_(self, on=True):
        # 累积监听通知形态（monitor_fn 之后使用），缺省同步
        self._async_monitor = on
        return self

    def apply(self):
        # 键级回调终结：按累积种类派发（后调覆盖先调）；未累积返回 Invalid 并记诊断
        if self._cb_kind == "checker":
            return _facade.chain_status(_facade.api_request(
                "/YomkContext/set_checker",
                ContextChecker(self._key, self._checker, self._group)))
        if self._cb_kind == "monitor":
            if self._async_monitor:
                # 异步形态：同步位 no-op 占位，异步位承载用户回调
                pkg = ContextMonitor(self._key, lambda c: None, self._monitor, self._group)
            else:
                pkg = ContextMonitor(self._key, self._monitor, False, self._group)
            return _facade.chain_status(
                _facade.api_request("/YomkContext/set_monitor", pkg))
        diag_error("apply: no checker/monitor accumulated")
        return ResStatus.Invalid

    def checker(self):
        # 组级校验门控开关分面
        return GroupSwitch(self._group, False)

    def monitor(self):
        # 组级变更监听开关分面
        return GroupSwitch(self._group, True)

    def keys(self):
        return _facade.api_request("/YomkContext/keys", self._group)

    def info(self):
        return _facade.api_request(
            "/YomkContext/key", ContextGroupKey(self._group, self._key))

    def groups(self):
        return _facade.api_request("/YomkContext/groups", None)

    def all(self):
        return _facade.api_request("/YomkContext/all", None)

    def msg(self, cls=None):
        # 切入读路径分面（cls 为类型指定位，忽略；get 不做类型校验）
        return ContextValueBuilder(self._key, self._group)


def ctx():
    # context 域链式入口
    return ContextBuilder()