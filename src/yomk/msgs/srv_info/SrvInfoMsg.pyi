from typing import List, Optional

from yomk.msgs.fn_pool import FuncInfo
from yomk.msgs.YomkMsg import YomkMsg


class ServerServiceInfo(YomkMsg):
    service_name: str
    funcs: List[FuncInfo]

    def __init__(self, service_name: str = "",
                 funcs: Optional[List[FuncInfo]] = None) -> None: ...


class ServerServiceInfoList(YomkMsg):
    items: list[ServerServiceInfo]

    def __init__(self, items: list[ServerServiceInfo] | None = None) -> None: ...
