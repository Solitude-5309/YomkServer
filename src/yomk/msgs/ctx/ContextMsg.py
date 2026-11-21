"""Context domain payloads.

ctx 域消息：组名 group 恒为构造参数最后一位，缺省 "main"。
"""

from enum import Enum
from yomk.msgs.YomkMsg import YomkMsg


class Context(YomkMsg):
    """Context create/set payload: key, value and owning group.

    上下文创建/写值载荷：键、值与所属组。
    """

    def __init__(self, key="", value=None, group="main"):
        self.key = key
        self.value = value
        self.group = group


class ContextKeyInfo(YomkMsg):
    """Introspection entry of one context key.

    单个上下文键的内省条目：类型名、门控与监听计数。
    """

    def __init__(self, key="", type_name="", checker_on=False,
                 monitor_count=0, async_monitor_count=0, group="main"):
        self.key = key
        self.type_name = type_name        # 值为 None 时为空串
        self.checker_on = checker_on
        self.monitor_count = monitor_count
        self.async_monitor_count = async_monitor_count
        self.group = group


class CheckStatus(Enum):
    """Checker verdict: Accept lets the write through, Reject blocks it.

    校验裁决：Accept 放行，Reject 拒绝。
    """

    eAccept = 0
    eReject = 1


class ContextChecker(YomkMsg):
    """Key-level checker payload.

    键级校验回调载荷：键、回调与所属组。
    """

    def __init__(self, key="", check_func=None, group="main"):
        self.key = key
        self.check_func = check_func
        self.group = group


class ContextMonitor(YomkMsg):
    """Key-level monitor payload: callback plus sync/async delivery flag.

    键级监听载荷：回调与同步/异步投递标志相互正交。
    """

    def __init__(self, key="", monitor_func=None, async_monitor=False, group="main"):
        self.key = key
        self.monitor_func = monitor_func
        self.async_monitor = async_monitor
        self.group = group


class ContextGroupKey(YomkMsg):
    """Group plus key addressing payload shared by destroy and key info.

    destroy 与 key 查询共用的组+键寻址载荷。
    """

    def __init__(self, group="main", key=""):
        self.group = group
        self.key = key


class ContextGroupInfo(YomkMsg):
    """One group with all its key introspection entries.

    单个组及其全部键的内省条目。
    """

    def __init__(self, group_name="", keys=None):
        self.group_name = group_name
        self.keys = keys if keys is not None else []  # 字典序


class ContextKeyInfoList(YomkMsg):
    """List payload of ContextKeyInfo.

    ContextKeyInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []


class ContextGroupInfoList(YomkMsg):
    """List payload of ContextGroupInfo.

    ContextGroupInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
