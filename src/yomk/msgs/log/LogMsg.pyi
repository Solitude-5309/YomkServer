from typing import Callable, Optional


class LogFile:
    # 文件日志器创建载荷（/YomkLogger/create_file_logger 请求数据）
    m_logger: str   # 日志器名
    m_dir: str      # 日志目录

    def __init__(self, logger: str = "", dir: str = "") -> None: ...


class Log:
    # 日志载荷（console_log/file_log/按级别开关端点共用）：日志级别为固定 int 常量，
    # 六级值序 Trace=0..Fatal=5
    eTrace: int
    eDebug: int
    eInfo: int
    eWarn: int
    eError: int
    eFatal: int

    m_level: int     # 级别（六级常量之一）
    m_log: str       # 日志内容
    m_logger: str    # 日志器名：服务端按其查找/惰性创建日志器实例
    m_tag: str       # 日志标签：随记录显示 [tag] 段，区别于 m_logger

    def __init__(self, level: int = 2, log_msg: str = "",
                 logger: str = "", tag: str = "") -> None: ...


class ConsoleLogProxy:
    # 控制台日志代理回调载体（/YomkLogger/set_console_log_proxy 请求数据）
    m_consoleLogProxyFunc: Optional[Callable[[Log], bool]]

    def __init__(self, console_log_proxy_func: Optional[Callable[[Log], bool]] = None) -> None: ...


class FileLogProxy:
    # 文件日志代理回调载体（/YomkLogger/set_file_log_proxy 请求数据；
    # 语义与控制台代理对称，两代理各自独立）
    m_fileLogProxyFunc: Optional[Callable[[Log], bool]]

    def __init__(self, file_log_proxy_func: Optional[Callable[[Log], bool]] = None) -> None: ...


class LoggerQuery:
    # 日志器单查请求（/YomkLogger/logger 请求数据：名 + 类型，两表允许同名须按类型区分）
    m_logger_name: str
    m_type: str  # 类型："console" 或 "file"

    def __init__(self, logger_name: str = "", type: str = "") -> None: ...


class LoggerState:
    # 日志器状态（trace/debug/info/warn/error/fatal/proxy 七开关）：console 条目
    # 承载控制台逐器门控层真值，file 条目承载文件逐器门控层真值；两套主闸相互独立
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


class LoggerInfo:
    # 日志器内省元信息（/YomkLogger/logger 返回数据、/YomkLogger/all 清单段元素）
    logger_name: str  # 日志器名
    type: str
    dir: str
    logger_state: LoggerState

    def __init__(self, logger_name: str = "", type: str = "",
                 dir: str = "",
                 logger_state: Optional[LoggerState] = None) -> None: ...