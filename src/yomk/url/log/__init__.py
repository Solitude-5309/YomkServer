# /YomkLogger/* 内置服务 URL 常量（对齐 C++ include/yomk/log/YomkLoggerUrl.h）。
# yomk.url 为纯命名空间层（与 yomk.msg 同构，无 __init__）：用户新增模块安装到 yomk 体系下时，
# 只需新增自己的命名空间子包并自带 __init__.py 即可并入本常量池。
__all__ = [
    "LOG_ALL", "LOG_CONSOLE_GLOBAL_STATE", "LOG_CONSOLE_LOG",
    "LOG_CREATE_FILE_LOGGER", "LOG_DELETE_LOGGER", "LOG_FILE_GLOBAL_STATE",
    "LOG_FILE_LOG", "LOG_LOGGER", "LOG_LOGGERS", "LOG_OFF_CONSOLE_LOG_BY_LEVEL",
    "LOG_OFF_FILE_LOG_BY_LEVEL", "LOG_ON_CONSOLE_LOG_BY_LEVEL",
    "LOG_ON_FILE_LOG_BY_LEVEL", "LOG_SET_CONSOLE_PROXY", "LOG_SET_FILE_LOG_PROXY",
    "LOG_WRITE_FILE_LOG",
]

LOG_SET_CONSOLE_PROXY = "/YomkLogger/set_console_log_proxy"
LOG_SET_FILE_LOG_PROXY = "/YomkLogger/set_file_log_proxy"
LOG_CONSOLE_LOG = "/YomkLogger/console_log"
LOG_CREATE_FILE_LOGGER = "/YomkLogger/create_file_logger"
LOG_FILE_LOG = "/YomkLogger/file_log"
LOG_WRITE_FILE_LOG = "/YomkLogger/write_file_log"
LOG_DELETE_LOGGER = "/YomkLogger/delete_logger"
LOG_ON_CONSOLE_LOG_BY_LEVEL = "/YomkLogger/on_console_log_by_level"
LOG_OFF_CONSOLE_LOG_BY_LEVEL = "/YomkLogger/off_console_log_by_level"
LOG_ON_FILE_LOG_BY_LEVEL = "/YomkLogger/on_file_log_by_level"
LOG_OFF_FILE_LOG_BY_LEVEL = "/YomkLogger/off_file_log_by_level"
LOG_LOGGERS = "/YomkLogger/loggers"
LOG_LOGGER = "/YomkLogger/logger"
LOG_ALL = "/YomkLogger/all"
LOG_FILE_GLOBAL_STATE = "/YomkLogger/file_global_state"
LOG_CONSOLE_GLOBAL_STATE = "/YomkLogger/console_global_state"
