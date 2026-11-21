"""Server info domain payloads.

服务器信息域消息：服务及其函数的内省条目。
"""

from yomk.msgs.YomkMsg import YomkMsg


class ServerServiceInfo(YomkMsg):
    """One service with its function introspection entries.

    单个服务及其函数内省条目。
    """

    def __init__(self, service_name="", funcs=None):
        self.service_name = service_name
        # 元素 func_name 为带 / 前缀的 url 段
        self.funcs = funcs if funcs is not None else []


class ServerServiceInfoList(YomkMsg):
    """List payload of ServerServiceInfo.

    ServerServiceInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
