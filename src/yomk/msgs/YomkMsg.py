"""YomkMsg —— 消息基类与通用解包（对齐 C++ include/yomk/msg/YomkMsg.h）

kName 为运行期类型名载体：继承即得、全限定、随类重命名自动跟随；
unpack 按类型解包：isinstance + kName 精确比对，未命中返回 None（对齐
C++ unpack nullptr 语义，判空即用，不抛）。
"""


class YomkMsg:
    """消息基类：继承即得 kName（运行期类型名），供 unpack 比对与报文文案。

    - kName 自动生成：父包.限定名（如 yomk.msgs.ctx.Context），
      随类重命名自动跟随，禁止手写；
    - 与 C++ kName 全限定口径一致（main 现行：根级宏亦带 yomk::msgs:: 前缀，无裸名）；
      需精确对齐跨语言字面时，类体显式定义 kName 覆盖自动值。
    """

    kName = ""                       # 基类自身不参与比对

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if "kName" not in cls.__dict__:          # 显式定义优先
            mod = cls.__module__
            parent, _, _ = mod.rpartition(".")
            cls.kName = (parent if parent else mod) + "." + cls.__qualname__


def unpack(pkg, cls):
    # 按类型解包：命中返回 pkg，未命中返回 None（对齐 C++ unpack nullptr 语义，判空即用，不抛）
    # 条件：isinstance(pkg, cls) 且 kName 比对通过（cls 无 kName 时退化为纯 isinstance）；
    # YomkMsg 体系内类型为精确身份匹配：子类必然获得与父类不同的自动 kName，
    # 子类实例解包为父类返回 None 是设计行为
    if not isinstance(pkg, cls):
        return None
    expected = getattr(cls, "kName", None)
    if not expected:
        return pkg
    if getattr(type(pkg), "kName", None) == expected:
        return pkg
    return None
