from enum import Enum


class Context:
    # key 所属组名随包携带（缺省 main；组名约定永远是 API 最后一个参数，下同）
    def __init__(self, key="", value=None, group="main"):
        self.key = key
        self.value = value
        self.group = group


class ContextKeyInfo:
    # Context 键内省元信息（/YomkContext/key 单查返回数据；同时是 ContextGroupInfo.keys 清单元素）
    def __init__(self, key="", type_name="", checker_on=False,
                 monitor_count=0, async_monitor_count=0, group="main"):
        self.key = key                    # 键名
        self.type_name = type_name        # 值类型名（值为 None 时为空串）
        self.checker_on = checker_on      # 门控是否开启
        self.monitor_count = monitor_count          # monitor 总数
        self.async_monitor_count = async_monitor_count  # 其中 async monitor 数
        self.group = group                # key 所属组名


class CheckStatus(Enum):
    eAccept = 0
    eReject = 1


class ContextChecker:
    def __init__(self, key="", check_func=None, group="main"):
        self.key = key
        self.check_func = check_func
        self.group = group                # 目标 key 所属组名（缺省 main）


class ContextMonitor:
    def __init__(self, key="", monitor_func=None, async_monitor_func=False, group="main"):
        self.key = key
        self.monitor_func = monitor_func
        self.async_monitor_func = async_monitor_func
        self.group = group                # 目标 key 所属组名（缺省 main）


class ContextGroupKey:
    # Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
    def __init__(self, group="main", key=""):
        self.group = group
        self.key = key


class ContextGroupInfo:
    # Context 组级内省条目（/YomkContext/all 返回数据元素：组名 + 该组下全部键元信息）
    def __init__(self, group_name="", keys=None):
        self.group_name = group_name      # 组名
        self.keys = keys if keys is not None else []  # 该组全部键的内省元信息（字典序）