from typing import Callable, Optional
from yomk.msgs.YomkMsg import YomkMsg


class LogFile(YomkMsg):
    m_logger: str
    m_dir: str

    def __init__(self, logger: str = "", dir: str = "") -> None: ...


class Log(YomkMsg):
    eTrace: int
    eDebug: int
    eInfo: int
    eWarn: int
    eError: int
    eFatal: int

    m_level: int
    m_log: str
    m_logger: str
    m_tag: str

    def __init__(self, level: int = 2, log_msg: str = "",
                 logger: str = "", tag: str = "") -> None: ...


class ConsoleLogProxy(YomkMsg):
    m_consoleLogProxyFunc: Optional[Callable[[Log], bool]]

    def __init__(self, console_log_proxy_func: Optional[Callable[[Log], bool]] = None) -> None: ...


class FileLogProxy(YomkMsg):
    m_fileLogProxyFunc: Optional[Callable[[Log], bool]]

    def __init__(self, file_log_proxy_func: Optional[Callable[[Log], bool]] = None) -> None: ...


class LoggerQuery(YomkMsg):
    m_logger_name: str
    m_type: str

    def __init__(self, logger_name: str = "", type: str = "") -> None: ...


class LoggerState(YomkMsg):
    trace_on: bool
    debug_on: bool
    info_on: bool
    warn_on: bool
    error_on: bool
    fatal_on: bool
    proxy_on: bool

    def __init__(self, trace_on: bool = False, debug_on: bool = False,
                 info_on: bool = False, warn_on: bool = False,
                 error_on: bool = False, fatal_on: bool = False,
                 proxy_on: bool = False) -> None: ...


class LoggerInfo(YomkMsg):
    logger_name: str
    type: str
    dir: str
    logger_state: LoggerState

    def __init__(self, logger_name: str = "", type: str = "",
                 dir: str = "",
                 logger_state: Optional[LoggerState] = None) -> None: ...


class LoggerInfoList(YomkMsg):
    items: list[LoggerInfo]

    def __init__(self, items: list[LoggerInfo] | None = None) -> None: ...
