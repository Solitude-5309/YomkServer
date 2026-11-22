from typing import Any, Callable, Optional
from yomk.msgs.YomkMsg import YomkMsg


class FuncInfo(YomkMsg):
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
    msg: Optional[YomkMsg]

    def __init__(self, name: str, msg: Optional[YomkMsg]) -> None: ...


class FuncInfoList(YomkMsg):
    items: list[FuncInfo]

    def __init__(self, items: list[FuncInfo] | None = None) -> None: ...
