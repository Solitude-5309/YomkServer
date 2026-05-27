from typing import Any, Callable, Dict
from enum import Enum
from readerwriterlock import rwlock

class ResStatus(Enum):
    eInvalid: int = -1
    eOk: int = 0
    eErr: int = 1

class YomkResponse:
    res_status: ResStatus
    msg: str
    data: Any

    def __init__(
        self,
        res_status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None
    ) -> None: ...

class YomkService:
    name: str
    server: Any
    functions: Dict[str, Callable[[Any], Any]]
    rwlock_functions: rwlock.RWLockFair

    def __init__(self, server: Any) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    def init(self) -> None: ...
    def install_func(self, name: str, function: Callable[[Any], Any]) -> None: ...
    def invoke(self, name: str, pkg: Any) -> Any: ...

class YomkServer:
    services: Dict[str, YomkService]
    rwlock_services: rwlock.RWLockFair

    def __init__(self) -> None: ...
    def add_service(self, service: YomkService) -> None: ...
    def request(
        self,
        url: str,
        pkg: Any
    ) -> Any: ...
    def async_request(
        self,
        url: str,
        pkg: Any,
        callback: Callable[[Any], None]
    ) -> None: ...
