"""Message base class and type-safe unpack.

消息基类与类型化解包：kName 继承即得、随重命名自动跟随；unpack 未命中返回 None。
"""


class YomkMsg:
    """Message base class; every subclass gets a qualified runtime kName.

    消息基类：子类自动获得全限定运行期类型名，供 unpack 比对与文案使用。

    Note:
        kName is generated as ``<parent package>.<qualified name>`` and must not
        be written by hand; define it explicitly only when a cross-language
        literal must be matched.
        kName 自动生成、随重命名跟随，禁止手写；仅在需要跨语言字面量对齐时显式覆盖。
    """

    kName = ""                       # 基类自身不参与比对

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if "kName" not in cls.__dict__:          # 显式定义优先
            mod = cls.__module__
            parent, _, _ = mod.rpartition(".")
            cls.kName = (parent if parent else mod) + "." + cls.__qualname__


def unpack(pkg, cls):
    """Return pkg when it matches cls by isinstance and kName, otherwise None.

    按 isinstance + kName 精确比对解包，未命中返回 None，不抛异常。
    子类因 kName 不同不会被当作父类实例命中，这是设计行为。
    """
    if not isinstance(pkg, cls):
        return None
    expected = getattr(cls, "kName", None)
    if not expected:
        return pkg
    if getattr(type(pkg), "kName", None) == expected:
        return pkg
    return None
