"""Standard built-in message set.

标准内置消息集：标量用 value 字段，数组用 items 字段，kName 由 YomkMsg 自动生成。
Bytes 为 Python 端独有，承载二进制载荷。
"""

from yomk.msgs.YomkMsg import YomkMsg


class Bool(YomkMsg):
    """Boolean scalar message.

    布尔标量消息。
    """

    def __init__(self, value=False):
        self.value = value


class Int64(YomkMsg):
    """64-bit integer scalar message.

    64 位整型标量消息。
    """

    def __init__(self, value=0):
        self.value = value


class Float64(YomkMsg):
    """64-bit float scalar message.

    64 位浮点标量消息。
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


def type_of(pkg):
    """Return the runtime kName of a message, or "null" for None and non-messages.

    返回消息的运行期 kName；None 与体系外对象返回 "null"。
    """
    if isinstance(pkg, YomkMsg):
        return type(pkg).kName
    return "null"


def type_mismatch(pkg, expected):
    """Build the standard type-mismatch text, expecting ``cls.kName``.

    构造标准类型不符文案；expected 由调用方传 ``cls.kName``。
    """
    return "pkg is not " + expected + ", got '" + type_of(pkg) + "'"
