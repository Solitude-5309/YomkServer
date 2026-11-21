from enum import Enum
from typing import Any, Callable, List, Optional
from yomk.msgs.YomkMsg import YomkMsg


class Context(YomkMsg):
    key: str
    value: Any
    group: str

    def __init__(self, key: str = "", value: Any = None,
                 group: str = "main") -> None: ...


class ContextKeyInfo(YomkMsg):
    key: str
    type_name: str
    checker_on: bool
    monitor_count: int
    async_monitor_count: int
    group: str

    def __init__(self, key: str = "", type_name: str = "",
                 checker_on: bool = False, monitor_count: int = 0,
                 async_monitor_count: int = 0,
                 group: str = "main") -> None: ...


class CheckStatus(Enum):
    eAccept = 0
    eReject = 1


class ContextChecker(YomkMsg):
    key: str
    check_func: Optional[Callable[[Context], CheckStatus]]
    group: str

    def __init__(self, key: str = "",
                 check_func: Optional[Callable[[Context], CheckStatus]] = None,
                 group: str = "main") -> None: ...


class ContextMonitor(YomkMsg):
    key: str
    monitor_func: Optional[Callable[[Context], None]]
    async_monitor: bool
    group: str

    def __init__(self, key: str = "",
                 monitor_func: Optional[Callable[[Context], None]] = None,
                 async_monitor: bool = False,
                 group: str = "main") -> None: ...


class ContextGroupKey(YomkMsg):
    group: str
    key: str

    def __init__(self, group: str = "main", key: str = "") -> None: ...


class ContextGroupInfo(YomkMsg):
    group_name: str
    keys: List[ContextKeyInfo]

    def __init__(self, group_name: str = "",
                 keys: Optional[List[ContextKeyInfo]] = None) -> None: ...


class ContextKeyInfoList(YomkMsg):
    items: list[ContextKeyInfo]

    def __init__(self, items: list[ContextKeyInfo] | None = None) -> None: ...


class ContextGroupInfoList(YomkMsg):
    items: list[ContextGroupInfo]

    def __init__(self, items: list[ContextGroupInfo] | None = None) -> None: ...
