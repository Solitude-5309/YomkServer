"""Chained context key-value builders for the ctx domain.

ctx 域链式上下文键值 Builder。
"""

from yomk._diag import error as diag_error
from yomk._diag import warn as diag_warn
from yomk.msgs.ctx import (
    Context,
    ContextChecker,
    ContextGroupInfoList,
    ContextGroupKey,
    ContextMonitor,
)
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import (
    _mismatch_text,
    _type_name_of,
    require_msg_instance,
    require_msg_subclass,
    unpack,
)
from yomk.server import _facade
from yomk.server.YomkService import _yomk_weak_slot_fill
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
    """Group-level switch facet entered by ctx().group(g).checker() / .monitor().

    组级开关分面：group(g).checker() / .monitor() 之后仅 enable() / disable()。
    """

    def __init__(self, group, monitor):
        self._group = group
        self._monitor = monitor  # False = 校验门控，True = 变更监听

    def enable(self):
        url = CTX_TURN_ON_MONITOR if self._monitor else CTX_TURN_ON_CHECKER
        return _facade.chain_status(_facade.api_request(url, String(self._group)))

    def disable(self):
        url = CTX_TURN_OFF_MONITOR if self._monitor else CTX_TURN_OFF_CHECKER
        return _facade.chain_status(_facade.api_request(url, String(self._group)))


class ContextValueBuilder:
    """Typed read facet: with_default() accumulates and get() terminates.

    类型化读分面：msg(cls) 之后 with_default() 累积、get() 终结。
    """

    def __init__(self, key, group, cls):
        # 期望类型必须是具体消息类：读值才可能按 TYPE_NAME 精确比对
        require_msg_subclass(cls, "context typed read class")
        self._key = key
        self._group = group
        self._cls = cls
        self._default = None

    def with_default(self, v):
        """Accumulate the fallback used when the read misses or mismatches.

        累积未命中/类型不符时的兜底值：必须是同 TYPE_NAME 的消息或 None。
        """
        if v is None:
            self._default = None
            return self
        # 裸值先过实例守卫；类型不符由期望类型单独判定
        require_msg_instance(v, "with_default")
        if v.TYPE_NAME == self._cls.TYPE_NAME:
            self._default = v
            return self
        raise TypeError(_mismatch_text("with_default requires '"
                                       + self._cls.TYPE_NAME + "'",
                                       _type_name_of(v)))

    def get(self):
        """Return the stored value, or the accumulated default when the read misses.

        返回存储值快照；未命中、未初始化或类型不匹配时回退累积的默认值，
        类型不匹配额外记 WARN 并携带期望/实际类型名。

        Warning:
            The snapshot is read-only; build a new value and set() instead of mutating it in place.
            返回只读快照，改动请构造新值再 set()，勿原地修改。
        """
        if _facade.current_server() is None:
            diag_error("YomkServer is not initialized")
            return self._default
        res = _facade.api_request(CTX_GET, Context(self._key, self._default, self._group))
        if res.status == ResStatus.eOk:
            if res.data is None:
                return self._default
            hit = unpack(res.data, self._cls)
            if hit:
                return hit.msg
            diag_warn("typed read context key '" + self._key
                      + "' type mismatch, expect '" + self._cls.TYPE_NAME
                      + "', got '" + _type_name_of(res.data)
                      + "', return default value")
            return self._default
        diag_error("get context failed: " + res.msg)
        return self._default


class ContextBuilder:
    """Chained builder for context key-value read and write within a group.

    上下文组内键值读写链式 Builder。

    Usage:
        Read:  ctx().key(k).group(g).msg(cls).with_default(d).get()
        Write: ctx().key(k).group(g).value(v).create() / .set() / .destroy()
        Group: ctx().group(g).checker().enable() / ctx().group(g).monitor().enable()
        Query: keys() / info() / groups() / all()

        读链经 msg(cls) 做类型比对后取值；写链终结返回三态 ResStatus；
        组级开关建组默认关；内省终结返回 Response。

    Warning:
        The builder only accumulates views; use it inline in one expression, never cache or reuse it.
        Builder 仅累积视图，只限单表达式内联使用，禁止缓存复用。
    """

    def __init__(self):
        self._key = ""
        self._group = "main"
        self._value = None
        self._cb_kind = None  # 后调覆盖先调
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
        if ctx is not None:
            require_msg_instance(ctx, "value()")
        self._value = ctx
        return self

    def create(self):
        """Create a context key; fails if the key exists or key/value is empty.

        创建上下文键并写入值；键已存在、键为空或值为空时失败，返回三态 ResStatus。
        """
        return _facade.chain_status(_facade.api_request(
            CTX_CREATE, Context(self._key, self._value, self._group)))

    def set(self):
        """Replace the whole context value.

        整体替换上下文值；键不存在、值为空、类型不匹配或被 checker 拒绝时失败，
        返回三态 ResStatus。
        """
        return _facade.chain_status(_facade.api_request(
            CTX_SET, Context(self._key, self._value, self._group)))

    def destroy(self):
        """Destroy a context key; fails if the key does not exist.

        销毁上下文键；键不存在时失败，返回三态 ResStatus。
        """
        return _facade.chain_status(_facade.api_request(
            CTX_DESTROY, ContextGroupKey(self._group, self._key)))

    def checker_fn(self, check_func):
        # checker 槽缺省填充 "check"；显式误配在注册期诊断
        self._checker = _yomk_weak_slot_fill(check_func, "checker", "check")
        self._cb_kind = "checker"
        return self

    def monitor_fn(self, monitor_func):
        # monitor 槽为 void 语义，缺省填充 "none"
        self._monitor = _yomk_weak_slot_fill(monitor_func, "monitor", "none")
        self._cb_kind = "monitor"
        return self

    def async_(self, on=True):
        # 监听通知形态：缺省同步
        self._async_monitor = on
        return self

    def apply(self):
        """Terminate with the accumulated checker or monitor callback.

        按累积种类派发键级回调；未累积任何回调返回 Invalid 并记诊断。
        """
        if self._cb_kind == "checker":
            return _facade.chain_status(_facade.api_request(
                CTX_SET_CHECKER,
                ContextChecker(self._key, self._checker, self._group)))
        if self._cb_kind == "monitor":
            # 单回调 + 布尔标志：async_monitor 决定同步派发或异步池投递
            msg = ContextMonitor(self._key, self._monitor,
                                 self._async_monitor, self._group)
            return _facade.chain_status(
                _facade.api_request(CTX_SET_MONITOR, msg))
        diag_error("apply: no checker/monitor accumulated")
        return ResStatus.Invalid

    def checker(self):
        return GroupSwitch(self._group, False)

    def monitor(self):
        return GroupSwitch(self._group, True)

    # —— 读路径终结: Response ——

    @staticmethod
    def _require_server():
        # 未初始化时直接返回错误 Response，由读终结原样返回
        server = _facade.current_server()
        if server is None:
            diag_error("YomkServer is not initialized")
            return Response(ResStatus.eInvalid, "YomkServer is not initialized")
        return None

    def keys(self):
        """Return all key names in the group as a plain list.

        返回组内全部键名（裸 list）；未初始化返回 Invalid，类型不符返回 No。
        """
        guard = self._require_server()
        if guard:
            return guard
        res = _facade.api_request(CTX_KEYS, String(self._group))
        if res.status == ResStatus.eOk:
            r = unpack(res.data, StringArray)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res

    def info(self):
        """Return introspection metadata of the target key.

        返回指定键的内省元信息；未初始化返回 Invalid。
        """
        guard = self._require_server()
        if guard:
            return guard
        return _facade.api_request(CTX_KEY, ContextGroupKey(self._group, self._key))

    def groups(self):
        """Return all group names as a plain list.

        返回全部组名（裸 list）；未初始化返回 Invalid，类型不符返回 No。
        """
        guard = self._require_server()
        if guard:
            return guard
        res = _facade.api_request(CTX_GROUPS, None)
        if res.status == ResStatus.eOk:
            r = unpack(res.data, StringArray)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res

    def all(self):
        """Return grouped introspection metadata as a plain list.

        返回全部组及其键的内省元信息（裸 list）；未初始化返回 Invalid，类型不符返回 No。
        """
        guard = self._require_server()
        if guard:
            return guard
        res = _facade.api_request(CTX_ALL, None)
        if res.status == ResStatus.eOk:
            r = unpack(res.data, ContextGroupInfoList)
            if not r:
                return Response(ResStatus.eNo, r.error)
            res.data = r.msg.items
        return res

    def msg(self, cls):
        """Enter the typed read facet; cls is the required expected message class.

        切入读路径分面；cls 为必填的期望消息类，get() 按 TYPE_NAME 精确比对。
        """
        return ContextValueBuilder(self._key, self._group, cls)


def ctx():
    """Return the ctx domain chained entry.

    返回 ctx 域链式入口。
    """
    return ContextBuilder()
