"""Message base class and type-safe unpack.

消息基类与类型化解包：TYPE_NAME 继承即得、随重命名自动跟随；unpack 未命中返回 None。
"""


class YomkMsg:
    """Message base class; every subclass gets a qualified runtime type name.

    消息基类：子类自动获得全限定运行期类型名，供 unpack 比对与文案使用。

    Note:
        ``TYPE_NAME`` is a class constant (PEP 8 upper snake case, no ``k``
        prefix) generated as ``<parent package>.<qualified name>``; it must not
        be written by hand, and may be overridden only when a cross-language
        literal must be matched.
        ``TYPE_NAME`` 按 Python 类常量惯例命名（全大写下划线，不带 C++ 的 k 前缀），
        自动生成、随重命名跟随，禁止手写；仅在需要跨语言字面量对齐时显式覆盖。
    """

    TYPE_NAME = ""                  # 基类自身不参与比对

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if "TYPE_NAME" not in cls.__dict__:      # 显式定义优先
            mod = cls.__module__
            parent, _, _ = mod.rpartition(".")
            cls.TYPE_NAME = (parent if parent else mod) + "." + cls.__qualname__


def unpack(pkg, cls):
    """Return pkg when it matches cls by isinstance and TYPE_NAME, otherwise None.

    按 isinstance + TYPE_NAME 精确比对解包，未命中返回 None，不抛异常。
    子类因 TYPE_NAME 不同不会被当作父类实例命中，这是设计行为。
    """
    if not isinstance(pkg, cls):
        return None
    expected = getattr(cls, "TYPE_NAME", None)
    if not expected:
        return pkg
    if getattr(type(pkg), "TYPE_NAME", None) == expected:
        return pkg
    return None
