"""Standard built-in message set.

标准内置消息集：标量用 value 字段，数组用 items 字段，TYPE_NAME 由 YomkMsg 自动生成。
Bytes 为 Python 端独有，承载二进制载荷。
"""

from yomk.msgs.YomkMsg import YomkMsg, arg_type_name


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


def type_of(pkg):
    """Return the runtime type name of a message, or an outward marker otherwise.

    返回消息的运行期类型名（TYPE_NAME）；None 返回 "null"，体系外对象返回
    "<non-YomkMsg xxx>"，不以 "null" 掩盖误用。
    """
    if pkg is None:
        return "null"
    if isinstance(pkg, YomkMsg) and type(pkg) is not YomkMsg:
        return type(pkg).TYPE_NAME
    return "<non-YomkMsg " + arg_type_name(pkg) + ">"


def type_mismatch(pkg, expected):
    """Build the standard type-mismatch text, expecting ``cls.TYPE_NAME``.

    构造标准类型不符文案；expected 由调用方传 ``cls.TYPE_NAME``。
    """
    return "pkg is not " + expected + ", got '" + type_of(pkg) + "'"
