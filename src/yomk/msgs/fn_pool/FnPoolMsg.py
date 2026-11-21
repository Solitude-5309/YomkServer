"""Function pool domain payloads.

函数池域消息：注册载荷、调用载荷与内省条目。
"""

from yomk.msgs.YomkMsg import YomkMsg


class FuncInfo(YomkMsg):
    """Function introspection entry.

    函数内省条目：函数名、期望消息类型与说明。
    """

    def __init__(self, func_name="", msg_name="", description=""):
        self.func_name = func_name
        self.msg_name = msg_name
        self.description = description


class Function(YomkMsg):
    """Function registration payload.

    函数注册载荷：名、回调、期望消息类型与说明。
    """

    def __init__(self, name, func, msg_name="", description=""):
        self.name = name
        self.func = func
        self.msg_name = msg_name
        self.description = description


class CallFunction(YomkMsg):
    """Function call payload.

    函数调用载荷：函数名与入参。
    """

    def __init__(self, name, pkg):
        self.name = name
        self.pkg = pkg


class FuncInfoList(YomkMsg):
    """List payload of FuncInfo.

    FuncInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
