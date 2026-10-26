from typing import Any, Callable, Dict, List
from enum import Enum


class ResStatus(Enum):
    eInvalid: int = -1
    eOk: int = 0
    eNo: int = 1


class CheckStatus(Enum):
    eAccept: int = 0
    eReject: int = 1


class YomkResponse:
    status: ResStatus   # 本次请求的状态
    msg: str            # 对状态的说明（不携带返回数据）
    data: Any           # 携带的返回数据

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None
    ) -> None: ...


class FuncInfo:
    func_name: str
    msg_name: str
    description: str
    def __init__(self, func_name: str = "", msg_name: str = "",
                 description: str = "") -> None: ...


class ServerServiceInfo:
    service_name: str
    funcs: List[FuncInfo]
    def __init__(self, service_name: str = "",
                 funcs: List[FuncInfo] = None) -> None: ...


class YomkService:
    name: str
    server: Any
    functions: Dict[str, Callable[[Any], YomkResponse]]
    function_infos: Dict[str, FuncInfo]
    registered: bool
    rwlock_functions: Any

    def __init__(self, server: Any) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    def mark_registered(self) -> None: ...
    def init(self) -> None: ...
    def deinit(self) -> None: ...

    def install_func(self, name: str, function: Callable[[
                     Any], YomkResponse], msg_name: str = "",
                     description: str = "") -> None: ...

    def func_infos(self) -> Dict[str, FuncInfo]: ...
    def invoke(self, name: str, pkg: Any) -> Any: ...
    def request(self, url: str, pkg: Any = None) -> Any: ...
    def async_request(self, url: str, pkg: Any = None,
                      callback: Callable[[Any], None] = None) -> None: ...


class Monitor:
    monitor_func: Callable[[Any], None]
    async_monitor_func: bool
    def __init__(self, monitor_func: Callable[[
                 Any], None] = None, async_monitor_func: bool = False) -> None: ...


class YomkServer:
    services: Dict[str, YomkService]
    rwlock_services: Any
    shutdown_done: bool
    executor: Any

    def __init__(self, async_thread_count: int = 0) -> None: ...
    @classmethod
    def create(cls, async_thread_count: int = 0) -> "YomkServer": ...
    def start_service(self, srv_names: List[str]) -> int: ...
    def new_service(self, srv_cls: type, srv_name: str = "") -> int: ...
    def add_service(self, service: YomkService) -> int: ...
    def del_service(self, srv_name: str) -> int: ...
    def shutdown(self) -> None: ...
    def service_names(self) -> List[str]: ...
    def service_func_infos(self, srv_name: str) -> Dict[str, FuncInfo]: ...
    def request(self, url: str, pkg: Any = None) -> Any: ...
    def async_request(self, url: str, pkg: Any = None,
                      callback: Callable[[Any], None] = None) -> None: ...


class Context:
    key: str
    value: Any
    group: str
    def __init__(self, key: str = "", value: Any = None,
                 group: str = "main") -> None: ...


class ContextKeyInfo:
    key: str
    type_name: str
    checker_on: bool
    monitor_count: int
    async_monitor_count: int
    group: str

    def __init__(self, key: str = "", type_name: str = "",
                 checker_on: bool = False, monitor_count: int = 0,
                 async_monitor_count: int = 0, group: str = "main") -> None: ...


class ContextChecker:
    key: str
    check_func: Callable[[Any], CheckStatus]
    group: str
    def __init__(self, key: str = "", check_func: Callable[[
                 Any], CheckStatus] = None, group: str = "main") -> None: ...


class ContextMonitor:
    key: str
    monitor_func: Callable[[Any], None]
    async_monitor_func: bool
    group: str

    def __init__(self, key: str = "", monitor_func: Callable[[
                 Any], None] = None, async_monitor_func: bool = False,
                 group: str = "main") -> None: ...


class ContextGroupKey:
    # Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键）
    group: str
    key: str
    def __init__(self, group: str = "main", key: str = "") -> None: ...


class ContextGroupInfo:
    # Context 组级内省条目（/YomkContext/all 返回数据元素：组名 + 该组下全部键元信息）
    group_name: str
    keys: List[ContextKeyInfo]
    def __init__(self, group_name: str = "",
                 keys: List[ContextKeyInfo] = None) -> None: ...


class Function:
    name: str
    func: Callable[[Any], YomkResponse]
    msg_name: str
    description: str

    def __init__(self, name: str = "", func: Callable[[
                 Any], YomkResponse] = None, msg_name: str = "",
                 description: str = "") -> None: ...


class CallFunction:
    name: str
    pkg: Any
    def __init__(self, name: str = "", pkg: Any = None) -> None: ...


class Event:
    m_eventLoopName: str
    m_pkg: Any
    m_serviceFunc: Callable[[Any], YomkResponse]
    m_eventId: int
    m_response: Any
    m_waitCallback: Callable[[], None]
    m_tag: str

    def __init__(
        self,
        event_loop_name: str = "",
        pkg: Any = None,
        func: Callable[[Any], YomkResponse] = None,
        tag: str = ""
    ) -> None: ...
    def handle(self) -> None: ...


class EventLoopPkg:
    m_eventloopName: str
    m_defaultServiceFunc: Callable[[Any], YomkResponse]
    m_msgName: str

    def __init__(
        self,
        event_loop_name: str = "",
        default_service_func: Callable[[Any], YomkResponse] = None,
        msg_name: str = ""
    ) -> None: ...


class EventLoopInfo:
    loop_name: str
    running: bool
    pending: int
    has_default_func: bool
    msg_name: str
    next_tags: List[str]

    def __init__(self, loop_name: str = "", running: bool = False,
                 pending: int = 0, has_default_func: bool = False,
                 msg_name: str = "", next_tags: List[str] = None) -> None: ...


class LoggerInfo:
    logger_name: str  # 日志器名
    # 类型："console" 或 "file"（与 LOGGERS 行文本 [console]/[file] 对应）
    type: str
    dir: str          # file 类型的日志目录（console 时为空串）
    # logger_state 状态段：console 条目承载控制台逐器门控层真值、file 条目承载
    # 文件逐器门控层真值（均为逐器层，全局主闸态经 console_global_state/
    # file_global_state 查询；文件代理落地前 file 侧 proxyOn 恒 false）
    logger_state: LoggerState

    def __init__(self, logger_name: str = "", type: str = "",
                 dir: str = "", logger_state: LoggerState = None) -> None: ...


class LoggerState:
    debug_on: bool    # debug 级别开关
    info_on: bool     # info 级别开关
    warn_on: bool     # warn 级别开关
    error_on: bool    # error 级别开关
    proxy_on: bool    # 代理已安装：console_global_state 报控制台代理、
    # file_global_state 报文件代理（文件代理落地前 file 侧恒 false）

    def __init__(self, debug_on: bool = False, info_on: bool = False,
                 warn_on: bool = False, error_on: bool = False,
                 proxy_on: bool = False) -> None: ...


class YomkContext(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def create(self, ctx: Context) -> YomkResponse: ...
    def destroy(self, groupKey: ContextGroupKey) -> YomkResponse: ...
    def get(self, ctx: Context) -> YomkResponse: ...
    def set(self, ctx: Context) -> YomkResponse: ...
    def turn_on_checker(self, pkg: Any) -> YomkResponse: ...
    def turn_off_checker(self, pkg: Any) -> YomkResponse: ...
    def set_checker(self, contextChecker: ContextChecker) -> YomkResponse: ...
    def turn_on_monitor(self, pkg: Any) -> YomkResponse: ...
    def turn_off_monitor(self, pkg: Any) -> YomkResponse: ...
    def set_monitor(self, contextMonitor: ContextMonitor) -> YomkResponse: ...
    def groups(self, pkg: Any) -> YomkResponse: ...
    def keys(self, pkg: Any) -> YomkResponse: ...
    def key_info(self, pkg: ContextGroupKey) -> YomkResponse: ...
    def list_all(self, pkg: Any) -> YomkResponse: ...


class YomkFunctionPool(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def registerFunction(self, function: Function) -> YomkResponse: ...
    def callFunction(self, call_func: CallFunction) -> YomkResponse: ...


class EventLoop:
    def __init__(self) -> None: ...
    def set_default_service_func(
        self, func: Callable[[Any], YomkResponse]) -> None: ...

    def run(self) -> None: ...
    def start(self) -> None: ...
    def stop(self) -> int: ...
    def post(self, event: Event) -> int: ...
    def post_wait(self, event: Event) -> int: ...


class YomkEventLoop(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def start(self, pkg: EventLoopPkg) -> YomkResponse: ...
    def stop(self, pkg: str) -> YomkResponse: ...
    def destroy(self, pkg: str) -> YomkResponse: ...
    def post(self, pkg: Event) -> YomkResponse: ...
    def post_wait(self, pkg: Event) -> YomkResponse: ...
