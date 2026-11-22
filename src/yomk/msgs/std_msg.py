"""Standard built-in message set.

标准内置消息集：标量用 value 字段，数组用 items 字段，TYPE_NAME 由 YomkMsg 自动生成。
Bytes 为 Python 端独有，承载二进制载荷。
"""

from yomk.msgs.YomkMsg import YomkMsg


class Bool(YomkMsg):
    """Boolean scalar message.

    布尔标量消息。
    """

    def __init__(self, value=False):
        self.value = value


class Int(YomkMsg):
    """Integer scalar message.

    整型标量消息；Python int 不区分位宽。
    """

    def __init__(self, value=0):
        self.value = value


class Float(YomkMsg):
    """Float scalar message.

    浮点标量消息；Python float 不区分位宽。
    """

    def __init__(self, value=0.0):
        self.value = value


class String(YomkMsg):
    """UTF-8 string scalar message.

    字符串标量消息。
    """

    def __init__(self, value=""):
        self.value = value


class Bytes(YomkMsg):
    """Binary payload message (Python-only).

    二进制载荷消息，Python 端独有。
    """

    def __init__(self, value=b""):
        self.value = value


class BoolArray(YomkMsg):
    """Boolean array message.

    布尔数组消息。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []


class IntArray(YomkMsg):
    """Integer array message.

    整型数组消息。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []


class FloatArray(YomkMsg):
    """Float array message.

    浮点数组消息。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []


class StringArray(YomkMsg):
    """String array message.

    字符串数组消息。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
