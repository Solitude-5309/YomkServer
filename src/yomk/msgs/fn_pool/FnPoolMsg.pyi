from typing import Any, Callable, Optional
from yomk.msgs.YomkMsg import YomkMsg


class FuncInfo(YomkMsg):
    # 功能函数元信息（/YomkFunctionPool/name 与 /all、/YomkServerInfo/function
    # 单查与 /all 清单段返回数据），预留扩展（如安全性校验）
    func_name: str
    msg_name: str
    description: str

    def __init__(
        self,
        func_name: str = "",
        msg_name: str = "",
        description: str = "",
    ) -> None: ...


class Function(YomkMsg):
    name: str
    func: Optional[Callable[[Any], Any]]
    msg_name: str
    description: str

    def __init__(self, name: str, func: Optional[Callable[[Any], Any]],
                 msg_name: str = "", description: str = "") -> None: ...


class CallFunction(YomkMsg):
    name: str
    pkg: Any

    def __init__(self, name: str, pkg: Any) -> None: ...


class FuncInfoList(YomkMsg):
    # FuncInfo 清单（对齐 C++ FuncInfoList；返回数据承载）
    items: list[FuncInfo]

    def __init__(self, items: list[FuncInfo] | None = None) -> None: ...
