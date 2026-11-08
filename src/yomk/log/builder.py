import sys

from yomk.log import _logSourceLoc
from yomk.msg.log import (
    ConsoleLogProxy,
    FileLogProxy,
    Log,
    LogFile,
    LoggerQuery,
)
from yomk.msg.server import ResStatus
from yomk.server import _facade


def _chain_log_content(loc, args):
    # 发射正文 = 位置前缀（进程级三开关）+ 逐参追加（str() 流式化、bool → true/false、
    # 单空格分隔；前缀为空时首参不加前导空格）
    msg = _logSourceLoc._yomk_source_loc_prefix(*loc)
    for arg in args:
        s = "true" if arg is True else ("false" if arg is False else str(arg))
        msg = msg + " " + s if msg else s
    return msg


class SinkLogBuilder:
    # 具名文件发射分面：log().to_sink(name) 之后 tag 累积并以级别方法终结
    def __init__(self, loc, sink):
        self._loc = loc
        self._sink = sink
        self._tag = "main"

    def tag(self, t="main"):
        self._tag = t
        return self

    def info(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def warn(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def error(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eError, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def debug(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def trace(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def fatal(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/file_log",
            Log(Log.eFatal, _chain_log_content(self._loc, args), self._sink, self._tag)))


class LogBuilder:
    # log 发射域 builder：主控制台发射本体（不经 to_sink 即主控制台路径）
    def __init__(self, loc):
        self._loc = loc
        self._tag = "main"

    def tag(self, t="main"):
        self._tag = t
        return self

    def to_sink(self, name):
        # 切入具名文件发射分面（未知名写入返回 Failed，不惰性创建）
        return SinkLogBuilder(self._loc, name)

    def info(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def warn(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def error(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eError, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def debug(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def trace(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def fatal(self, *args):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/console_log",
            Log(Log.eFatal, _chain_log_content(self._loc, args), self._tag, self._tag)))


class ConsoleMgrBuilder:
    # log_mgr 控制台管理分面：门控/代理/内省
    def __init__(self, name):
        self._name = name
        self._level = Log.eInfo
        self._proxy = None

    def level(self, lv):
        self._level = lv
        return self

    def enable(self):
        # name 空 = 全局主闸，具名 = 逐器门控
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/on_console_log_by_level", Log(self._level, "", self._name)))

    def disable(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/off_console_log_by_level", Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        # None = 恢复默认输出
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/set_console_log_proxy", ConsoleLogProxy(self._proxy)))

    def list(self):
        return _facade.api_request("/YomkLogger/loggers", "console")

    def get_info(self):
        return _facade.api_request(
            "/YomkLogger/logger", LoggerQuery(self._name, "console"))

    def status(self):
        # 全局主闸状态（6 级别 + proxy，与文件侧主闸相互独立）
        return _facade.api_request("/YomkLogger/console_global_state", None)


class FileMgrBuilder:
    # log_mgr 文件管理分面：门控/代理/内省/生命周期
    def __init__(self, name, dir_):
        self._name = name
        self._dir = dir_
        self._level = Log.eInfo
        self._proxy = None

    def level(self, lv):
        self._level = lv
        return self

    def enable(self):
        # name 空 = 文件全局主闸，具名 = 逐器门控（未知名 Failed）
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/on_file_log_by_level", Log(self._level, "", self._name)))

    def disable(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/off_file_log_by_level", Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/set_file_log_proxy", FileLogProxy(self._proxy)))

    def list(self):
        return _facade.api_request("/YomkLogger/loggers", "file")

    def get_info(self):
        return _facade.api_request(
            "/YomkLogger/logger", LoggerQuery(self._name, "file"))

    def status(self):
        return _facade.api_request("/YomkLogger/file_global_state", None)

    def create(self):
        # 创建文件日志器（dir 累积自 log_mgr().dir()）
        return _facade.chain_status(_facade.api_request(
            "/YomkLogger/create_file_logger", LogFile(self._name, self._dir)))

    def flush(self):
        return _facade.chain_status(
            _facade.api_request("/YomkLogger/write_file_log", self._name))

    def remove(self):
        return _facade.chain_status(
            _facade.api_request("/YomkLogger/delete_logger", self._name))


class LocBuilder:
    # 位置前缀三开关分面：构造承接当前进程状态，apply 一次性落三开关（恒 Succeeded）
    def __init__(self):
        self._show_file = _logSourceLoc._yomk_loc_show_file
        self._show_line = _logSourceLoc._yomk_loc_show_line
        self._show_func = _logSourceLoc._yomk_loc_show_func

    def show_file(self, on):
        self._show_file = on
        return self

    def show_line(self, on):
        self._show_line = on
        return self

    def show_func(self, on):
        self._show_func = on
        return self

    def apply(self):
        _logSourceLoc._yomk_loc_show_file = self._show_file
        _logSourceLoc._yomk_loc_show_line = self._show_line
        _logSourceLoc._yomk_loc_show_func = self._show_func
        return ResStatus.Succeeded


class LogMgrBuilder:
    # log 管理域 builder：dir 累积 + console/file/location 分面 + 全量内省
    def __init__(self):
        self._dir = ""

    def dir(self, d):
        # 须先于 file() 累积（create 用）
        self._dir = d
        return self

    def file(self, name=""):
        return FileMgrBuilder(name, self._dir)

    def console(self, name=""):
        return ConsoleMgrBuilder(name)

    def location(self):
        return LocBuilder()

    def all(self):
        return _facade.api_request("/YomkLogger/all", None)


def log():
    # log 发射域链式入口；调用行位置在此捕获，
    # 仅限单表达式内联使用，禁止缓存复用 Builder
    frame = sys._getframe(1)
    return LogBuilder((frame.f_code.co_filename, frame.f_lineno, frame.f_code.co_name))


def log_mgr():
    # log 管理域链式入口
    return LogMgrBuilder()