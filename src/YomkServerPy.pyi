from typing import Any, Callable, Dict
from enum import Enum
from readerwriterlock import rwlock

class ResStatus(Enum):
    eInvalid: int = -1
    eOk: int = 0
    eErr: int = 1
    
class CheckStatus(Enum):
    eAccept: int = 0,
    eReject: int = 1

class YomkResponse:
    status: ResStatus
    msg: str
    data: Any

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None
    ) -> None: ...

class YomkService:
    name: str
    server: Any
    functions: Dict[str, Callable[[Any], YomkResponse]]
    rwlock_functions: rwlock.RWLockFair

    def __init__(self, server: Any) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    def init(self) -> None: ...
    def install_func(self, name: str, function: Callable[[Any], YomkResponse]) -> None: ...
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

class Context:
    key: str
    value: Any
    def __init__(
        self,
        key: str = "",
        value: Any = None
    ) -> None: ...
    
class ContextChecker:
    key: str
    check_func: Callable[[Any], CheckStatus]
    def __init__(self, key="", check_func=None) -> None: ...

class ContextMonitor:
    key: str
    monitor_func: Callable[[Any], None]
    def __init__(self, key="", monitor_func=None) -> None: ...

class Function:
    name: str
    func: Callable[[Any], YomkResponse]
    def __init__(self, name="", func=None) -> None: ...
    
class CallFunction:
    name: str
    pkg: Any
    def __init__(self, name="", pkg=None) -> None: ...

class Event:
    event_loop_name: str
    pkg: Any
    func: Callable[[Any], YomkResponse]
    def __init__(self, event_loop_name="", pkg=None, func=None) -> None: ...
    
class EventLoopPkg:
    event_loop_name: str
    default_service_func: Callable[[Any], YomkResponse]
    def __init__(self, event_loop_name="", default_service_func=None) -> None: ...