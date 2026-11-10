from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn
from yomk.msg.ctx import (
    Context,
    ContextChecker,
    ContextGroupKey,
    ContextMonitor,
)
from yomk.msg.server import ResStatus, Response
from yomk.server import _facade
from yomk.url.ctx import (
    CTX_ALL,
    CTX_CREATE,
    CTX_DESTROY,
    CTX_GET,
    CTX_GROUPS,
    CTX_KEY,
    CTX_KEYS,
    CTX_SET,
    CTX_SET_CHECKER,
    CTX_SET_MONITOR,
    CTX_TURN_OFF_CHECKER,
    CTX_TURN_OFF_MONITOR,
    CTX_TURN_ON_CHECKER,
    CTX_TURN_ON_MONITOR,
)


class GroupSwitch:
    # 组级开关分面：ctx().group(g).checker()/monitor() 之后仅 enable()/disable()
    def __init__(self, group, monitor):
        self._group = group
        self._monitor = monitor  # False = 校验门控，True = 变更监听

    def enable(self):
        url = CTX_TURN_ON_MONITOR if self._monitor else CTX_TURN_ON_CHECKER
        return _facade.chain_status(_facade.api_request(url, self._group))

    def disable(self):
        url = CTX_TURN_OFF_MONITOR if self._monitor else CTX_TURN_OFF_CHECKER
        return _facade.chain_status(_facade.api_request(url, self._group))


class ContextValueBuilder:
    # 读路径分面：ctx().key(n).group(g).msg(cls) 之后 with_default(v).get()
    # 直取值本体；cls 非 None 时做类型比对（U-09，对齐 C++ ContextValue<T>
    # kName 比对），不符 WARN 携 expect/got 并回退默认值
    def __init__(self, key, group, cls=None):
        self._key = key
        self._group = group
        self._cls = cls
        self._default = None

    def with_default(self, v):
        self._default = v
        return self

    def get(self):
        # 未初始化：C++ requireServer 记 ERROR 诊断后回退默认值
        if _facade.current_server() is None:
            diag_error("YomkServer is not initialized")
            return self._default
        res = _facade.api_request(CTX_GET, Context(self._key, self._default, self._group))
        if res.status == ResStatus.eOk:
            # 类型比对：cls 为 None 时跳过（动态超集）；命中即返回
            if res.data is None or self._cls is None \
                    or type(res.data).__name__ == self._cls.__name__:
                return res.data
            # 具体值原则：类型不匹配静默回退默认值会掩盖写入侧错配，
            # 输出期望/实际类型名定位（对齐 C++ YomkContextApi.h:163-164）
            diag_warn("typed read context key '" + self._key
                      + "' type mismatch, expect '" + self._cls.__name__
                      + "', got '" + type(res.data).__name__
                      + "', return default value")
            return self._default
        diag_error("get context failed: " + res.msg)
        return self._default


class ContextBuilder:
    # context 域 builder：key/group/value 累积 + 写终结三态收敛 + 读终结 Response
    # + msg(cls) 类型化分面。写路径未初始化由请求域收敛（对齐 C++），读终结
    # 五处（get/keys/info/groups/all）自带 requireServer 守卫
    def __init__(self):
        self._key = ""
        self._group = "main"
        self._value = None
        self._cb_kind = None  # "checker" / "monitor" / None（后调覆盖先调）
        self._checker = None
        self._monitor = None
        self._async_monitor = False

    def key(self, name):
        self._key = name
        return self

    def group(self, name="main"):
        self._group = name
        return self

    def value(self, ctx):
        self._value = ctx
        return self

    def create(self):
        return _facade.chain_status(_facade.api_request(
            CTX_CREATE, Context(self._key, self._value, self._group)))

    def set(self):
        return _facade.chain_status(_facade.api_request(
            CTX_SET, Context(self._key, self._value, self._group)))

    def destroy(self):
        return _facade.chain_status(_facade.api_request(
            CTX_DESTROY, ContextGroupKey(self._group, self._key)))

    def checker_fn(self, check_func):
        self._checker = check_func
        self._cb_kind = "checker"
        return self

    def monitor_fn(self, monitor_func):
        self._monitor = monitor_func
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
                CTX_SET_CHECKER,
                ContextChecker(self._key, self._checker, self._group)))
        if self._cb_kind == "monitor":
            if self._async_monitor:
                # 异步形态：同步位 no-op 占位，异步位承载用户回调
                pkg = ContextMonitor(self._key, lambda c: None, self._monitor, self._group)
            else:
                pkg = ContextMonitor(self._key, self._monitor, False, self._group)
            return _facade.chain_status(
                _facade.api_request(CTX_SET_MONITOR, pkg))
        diag_error("apply: no checker/monitor accumulated")
        return ResStatus.Invalid

    def checker(self):
        # 组级校验门控开关分面
        return GroupSwitch(self._group, False)

    def monitor(self):
        # 组级变更监听开关分面
        return GroupSwitch(self._group, True)

    # —— 读路径终结：自带 requireServer 守卫（对齐 C++ ContextBuilder
    #    keys/info/groups/all 与 ContextValue::get）——
    @staticmethod
    def _require_server():
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return Response(ResStatus.eInvalid, "YomkServer is not initialized")
        return None

    def keys(self):
        guard = self._require_server()
        if guard:
            return guard
        return _facade.api_request(CTX_KEYS, self._group)

    def info(self):
        guard = self._require_server()
        if guard:
            return guard
        return _facade.api_request(CTX_KEY, ContextGroupKey(self._group, self._key))

    def groups(self):
        guard = self._require_server()
        if guard:
            return guard
        return _facade.api_request(CTX_GROUPS, None)

    def all(self):
        guard = self._require_server()
        if guard:
            return guard
        return _facade.api_request(CTX_ALL, None)

    def msg(self, cls=None):
        # 切入读路径分面；cls 为期望类型（对齐 C++ msg<T>() kName 比对对象），
        # 省略时 get() 不做类型校验
        return ContextValueBuilder(self._key, self._group, cls)


def ctx():
    # context 域链式入口
    return ContextBuilder()
