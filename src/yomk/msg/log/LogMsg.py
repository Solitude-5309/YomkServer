class LogFile:
    def __init__(self, logger="", dir=""):
        self.m_logger = logger
        self.m_dir = dir


class Log:
    # Log：六级日志级别为固定 int 常量，值序 Trace=0..Fatal=5，
    # 非法值注入合法化（log 路径 default 降级 Info 分支可触达）
    eTrace = 0
    eDebug = 1
    eInfo = 2
    eWarn = 3
    eError = 4
    eFatal = 5

    def __init__(self, level=eInfo, log_msg="", logger="", tag=""):
        self.m_level = level
        self.m_log = log_msg
        self.m_logger = logger   # 日志器名：服务端按其查找/惰性创建日志器实例
        self.m_tag = tag         # 日志标签：随记录显示 [tag] 段，区别于 m_logger


class ConsoleLogProxy:
    def __init__(self, console_log_proxy_func=None):
        # func(Log) -> bool，None 等价卸载
        self.m_consoleLogProxyFunc = console_log_proxy_func


class FileLogProxy:
    # 文件日志代理回调载体（/YomkLogger/set_file_log_proxy 请求数据；
    # 语义与控制台代理对称，两代理各自独立）
    def __init__(self, file_log_proxy_func=None):
        # func(Log) -> bool，None 等价卸载
        self.m_fileLogProxyFunc = file_log_proxy_func


class LoggerQuery:
    # 日志器单查请求（/YomkLogger/logger 请求数据：名 + 类型，两表允许同名须按类型区分）
    def __init__(self, logger_name="", type=""):
        self.m_logger_name = logger_name
        self.m_type = type  # 类型："console" 或 "file"


class LoggerInfo:
    # 日志器内省元信息（/YomkLogger/logger 返回数据、/YomkLogger/all 清单段元素）
    def __init__(self, logger_name="", type="", dir="", logger_state=None):
        self.logger_name = logger_name  # 日志器名
        # 类型："console" 或 "file"（loggers 端点按此类型参数查询）
        self.type = type
        self.dir = dir                  # file 类型的日志目录（console 时为空串）
        # 状态段（成员 logger_state，类型 LoggerState）：
        # console 条目承载控制台逐器门控层真值（console 全局主闸态经
        # /console_global_state 查询）；file 条目承载文件逐器门控层真值
        # （file 全局主闸态经 /file_global_state 查询）
        self.logger_state = (logger_state if logger_state is not None
                             else LoggerState())


class LoggerState:
    # 日志器状态（trace/debug/info/warn/error/fatal/proxy 七开关）：console 条目
    # 承载控制台逐器门控层真值，console 全局主闸态经 /console_global_state 查询；
    # file 条目承载文件逐器门控层真值，file 全局主闸态经 /file_global_state
    # 查询；两套主闸相互独立
    def __init__(self, trace_on=False, debug_on=False, info_on=False,
                 warn_on=False, error_on=False, fatal_on=False,
                 proxy_on=False):
        self.trace_on = trace_on        # trace 级别开关
        self.debug_on = debug_on        # debug 级别开关
        self.info_on = info_on          # info 级别开关
        self.warn_on = warn_on          # warn 级别开关
        self.error_on = error_on        # error 级别开关
        self.fatal_on = fatal_on        # fatal 级别开关
        # proxy_on：代理已安装——/console_global_state 报控制台代理、
        # /file_global_state 报文件代理（两套代理各自独立）
        self.proxy_on = proxy_on