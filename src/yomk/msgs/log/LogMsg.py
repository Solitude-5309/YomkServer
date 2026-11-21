"""Log domain payloads.

日志域消息：六级级别、代理回调与内省快照。
"""

from yomk.msgs.YomkMsg import YomkMsg


class LogFile(YomkMsg):
    """File logger creation payload: logger name and directory.

    文件日志器创建载荷：日志器名与目录。
    """

    def __init__(self, logger="", dir=""):
        self.m_logger = logger
        self.m_dir = dir


class Log(YomkMsg):
    """Log record payload; level constants run Trace=0 to Fatal=5.

    日志记录载荷；级别常量 Trace=0..Fatal=5。
    """

    eTrace = 0
    eDebug = 1
    eInfo = 2
    eWarn = 3
    eError = 4
    eFatal = 5

    def __init__(self, level=eInfo, log_msg="", logger="", tag=""):
        self.m_level = level
        self.m_log = log_msg
        self.m_logger = logger   # 日志器名：服务端按其查找或惰性创建
        self.m_tag = tag         # 日志标签：记录显示 [tag] 段，区别于 m_logger


class ConsoleLogProxy(YomkMsg):
    """Console proxy callback payload; None uninstalls it.

    控制台代理回调载荷；回调签名为 func(Log) -> bool，None 表示卸载。
    """

    def __init__(self, console_log_proxy_func=None):
        self.m_consoleLogProxyFunc = console_log_proxy_func


class FileLogProxy(YomkMsg):
    """File proxy callback payload; None uninstalls it.

    文件代理回调载荷；回调签名为 func(Log) -> bool，None 表示卸载，与控制台代理相互独立。
    """

    def __init__(self, file_log_proxy_func=None):
        self.m_fileLogProxyFunc = file_log_proxy_func


class LoggerQuery(YomkMsg):
    """Single logger query; name plus type because both tables may share names.

    单个日志器查询载荷：名+类型，控制台与文件两表允许同名。
    """

    def __init__(self, logger_name="", type=""):
        self.m_logger_name = logger_name
        self.m_type = type  # "console" 或 "file"


class LoggerInfo(YomkMsg):
    """Logger introspection entry: name, type, directory and state.

    日志器内省条目：名、类型、目录与逐器门控状态。
    """

    def __init__(self, logger_name="", type="", dir="", logger_state=None):
        self.logger_name = logger_name
        self.type = type
        self.dir = dir                  # file 类型的日志目录，console 为空串
        # 逐器门控真值；全局主闸经 /console_global_state 或 /file_global_state 查询
        self.logger_state = (logger_state if logger_state is not None
                             else LoggerState())


class LoggerState(YomkMsg):
    """Per-logger switch state: six levels plus the proxy flag.

    逐器门控状态：六个级别开关与代理标志；控制台与文件两套代理相互独立。
    """

    def __init__(self, trace_on=False, debug_on=False, info_on=False,
                 warn_on=False, error_on=False, fatal_on=False,
                 proxy_on=False):
        self.trace_on = trace_on
        self.debug_on = debug_on
        self.info_on = info_on
        self.warn_on = warn_on
        self.error_on = error_on
        self.fatal_on = fatal_on
        self.proxy_on = proxy_on


class LoggerInfoList(YomkMsg):
    """List payload of LoggerInfo.

    LoggerInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
