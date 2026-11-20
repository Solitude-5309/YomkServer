"""std_msg —— 标准内置消息集与文案构造（对齐 C++ include/yomk/msg/YomkStdMsg.h + typeOf/typeMismatch）

标准内置集为真实 msg 类直译（A 方案）：端点载荷为 std 类实例，unpack 走完整
精确语义；标量类字段统一 value、数组类字段统一 items；kName 由 YomkMsg
基类自动生成（yomk.msgs.String 等全限定形态，无手写）。

C++ 30 个 std msg 收敛为 9 类：11 个整型/字符标量变体 → Int64、
Float32/Float64 → Float64、11 个整型/字符 *Array → IntArray；Bytes 为
Python 端新增（二进制载荷是 Python 基础类型）。
"""

from yomk.msgs.YomkMsg import YomkMsg


class Bool(YomkMsg):
    def __init__(self, value=False):
        self.value = value


class Int64(YomkMsg):
    def __init__(self, value=0):
        self.value = value


class Float64(YomkMsg):
    def __init__(self, value=0.0):
        self.value = value


class String(YomkMsg):
    def __init__(self, value=""):
        self.value = value


class Bytes(YomkMsg):
    def __init__(self, value=b""):
        self.value = value


class BoolArray(YomkMsg):
    def __init__(self, items=None):
        self.items = items if items is not None else []


class IntArray(YomkMsg):
    def __init__(self, items=None):
        self.items = items if items is not None else []


class FloatArray(YomkMsg):
    def __init__(self, items=None):
        self.items = items if items is not None else []


class StringArray(YomkMsg):
    def __init__(self, items=None):
        self.items = items if items is not None else []


def type_of(pkg):
    # 消息的运行时类型名，判空安全（对齐 C++ typeOf，两态）：
    # YomkMsg 实例 → type(pkg).kName；None 及体系外对象（非消息）→ "null"
    if isinstance(pkg, YomkMsg):
        return type(pkg).kName
    return "null"


def type_mismatch(pkg, expected):
    # 构造标准类型不符文案（对齐 C++ typeMismatch）：
    # "pkg is not <expected>, got '<type_of(pkg)>'"；expected 由调用方传 cls.kName
    return "pkg is not " + expected + ", got '" + type_of(pkg) + "'"
