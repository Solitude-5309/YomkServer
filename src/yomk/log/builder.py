"""Chained logging builders for emission and management.

日志发射与管理链式 Builder。
"""

import sys

from yomk.log import _logSourceLoc  # 私有子模块：不随包根 __init__ 导出
from yomk.msgs.std_msg import String, StringArray
from yomk.msgs.YomkMsg import unpack
from yomk.msgs.log import (
    LoggerInfoList,
    ConsoleLogProxy,
    FileLogProxy,
    Log,
    LogFile,
    LoggerQuery,
)
from yomk.msgs.server import ResStatus
from yomk.server import _facade
from yomk.url.log import (
    LOG_ALL,
    LOG_CONSOLE_GLOBAL_STATE,
    LOG_CONSOLE_LOG,
    LOG_CREATE_FILE_LOGGER,
    LOG_DELETE_LOGGER,
    LOG_FILE_GLOBAL_STATE,
    LOG_FILE_LOG,
    LOG_LOGGER,
    LOG_LOGGERS,
    LOG_OFF_CONSOLE_LOG_BY_LEVEL,
    LOG_OFF_FILE_LOG_BY_LEVEL,
    LOG_ON_CONSOLE_LOG_BY_LEVEL,
    LOG_ON_FILE_LOG_BY_LEVEL,
    LOG_SET_CONSOLE_PROXY,
    LOG_SET_FILE_LOG_PROXY,
    LOG_WRITE_FILE_LOG,
)


def _chain_log_content(loc, args):
    # 正文 = 位置前缀 + 逐参追加：单空格分隔，bool → true/false，
    # 浮点按 6 位有效数字输出，其余用 str()
    msg = _logSourceLoc._yomk_source_loc_prefix(*loc)
    for arg in args:
        s = ("true" if arg is True else
             ("false" if arg is False else
              ("%.6g" % arg if isinstance(arg, float) else str(arg))))
        msg = msg + " " + s if msg else s
    return msg


class SinkLogBuilder:
    """Named file emission facet entered by log().to_sink(name).

    具名文件发射分面：to_sink(name) 之后 tag 累积、六级方法终结。

    Note:
        Level terminators block until the write completes and return Invalid if uninitialized.
        六级发射方法阻塞至写入完成，未初始化返回 Invalid。
    """

    def __init__(self, loc, sink):
        self._loc = loc
        self._sink = sink
        self._tag = "main"

    def tag(self, t):
        self._tag = t
        return self

    def info(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def warn(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def error(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eError, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def debug(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def trace(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def fatal(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_FILE_LOG,
            Log(Log.eFatal, _chain_log_content(self._loc, args), self._sink, self._tag)))


class LogBuilder:
    """Main console emission builder returned by log().

    主控制台日志发射 Builder。

    Note:
        Level terminators block until the write completes; the source location
        is captured at the log() call site, not at the terminator.
        六级方法阻塞至写入完成，位置取自 log() 调用行；未初始化返回 Invalid。
    """

    def __init__(self, loc):
        self._loc = loc
        self._tag = "main"

    def tag(self, t):
        self._tag = t
        return self

    def to_sink(self, name):
        """Switch to the named file emission facet.

        切入具名文件发射分面；未知名在写入时失败，不做惰性创建。
        """
        return SinkLogBuilder(self._loc, name)

    def info(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def warn(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def error(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eError, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def debug(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def trace(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def fatal(self, *args):
        return _facade.chain_status(_facade.api_request(
            LOG_CONSOLE_LOG,
            Log(Log.eFatal, _chain_log_content(self._loc, args), self._tag, self._tag)))


class ConsoleMgrBuilder:
    """Console management facet: gating, proxy and introspection.

    控制台管理分面：门控、代理与内省。

    Note:
        Empty name switches the global master gate, a name targets one logger;
        level defaults to Info and proxy(None) restores the default sink.
        Console and file master gates are independent.
        name 为空 = 全局主闸，具名 = 逐器门控；level 缺省 Info；proxy(None) 恢复默认输出；
        控制台与文件主闸相互独立。
    """

    def __init__(self, name):
        self._name = name
        self._level = Log.eInfo
        self._proxy = None

    def level(self, lv):
        self._level = lv
        return self

    def enable(self):
        return _facade.chain_status(_facade.api_request(
            LOG_ON_CONSOLE_LOG_BY_LEVEL, Log(self._level, "", self._name)))

    def disable(self):
        return _facade.chain_status(_facade.api_request(
            LOG_OFF_CONSOLE_LOG_BY_LEVEL, Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        return _facade.chain_status(_facade.api_request(
            LOG_SET_CONSOLE_PROXY, ConsoleLogProxy(self._proxy)))

    def list(self):
        res = _facade.api_request(LOG_LOGGERS, String("console"))
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, StringArray).items
        return res

    def get_info(self):
        return _facade.api_request(
            LOG_LOGGER, LoggerQuery(self._name, "console"))

    def status(self):
        return _facade.api_request(LOG_CONSOLE_GLOBAL_STATE, None)


class FileMgrBuilder:
    """File management facet: gating, proxy, lifecycle and introspection.

    文件管理分面：门控、代理、生命周期与内省。

    Note:
        Empty name switches the file global gate; gate toggles on unknown names
        fail, while create() creates the logger from the directory accumulated
        by log_mgr().dir().
        name 为空 = 文件全局主闸；未知名门控失败，create() 按 dir() 累积目录创建。
    """

    def __init__(self, name, dir_):
        self._name = name
        self._dir = dir_
        self._level = Log.eInfo
        self._proxy = None

    def level(self, lv):
        self._level = lv
        return self

    def enable(self):
        return _facade.chain_status(_facade.api_request(
            LOG_ON_FILE_LOG_BY_LEVEL, Log(self._level, "", self._name)))

    def disable(self):
        return _facade.chain_status(_facade.api_request(
            LOG_OFF_FILE_LOG_BY_LEVEL, Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        return _facade.chain_status(_facade.api_request(
            LOG_SET_FILE_LOG_PROXY, FileLogProxy(self._proxy)))

    def list(self):
        res = _facade.api_request(LOG_LOGGERS, String("file"))
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, StringArray).items
        return res

    def get_info(self):
        return _facade.api_request(
            LOG_LOGGER, LoggerQuery(self._name, "file"))

    def status(self):
        return _facade.api_request(LOG_FILE_GLOBAL_STATE, None)

    def create(self):
        return _facade.chain_status(_facade.api_request(
            LOG_CREATE_FILE_LOGGER, LogFile(self._name, self._dir)))

    def flush(self):
        return _facade.chain_status(
            _facade.api_request(LOG_WRITE_FILE_LOG, String(self._name)))

    def remove(self):
        return _facade.chain_status(
            _facade.api_request(LOG_DELETE_LOGGER, String(self._name)))


class LocBuilder:
    """Location prefix facet with three switches applied at once.

    位置前缀三开关分面：apply() 一次性落盘，恒返回 Succeeded。
    """

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
    """Log management builder returned by log_mgr().

    日志管理 Builder：dir 累积 + console/file/location 分面 + 全量内省。

    Note:
        Call dir() before file().create(); a later dir() does not affect builders
        that were already created.
        dir() 须先于 file().create()，创建时按累积值快照。
    """

    def __init__(self):
        self._dir = ""

    def dir(self, d):
        self._dir = d
        return self

    def file(self, name=""):
        return FileMgrBuilder(name, self._dir)

    def console(self, name=""):
        return ConsoleMgrBuilder(name)

    def location(self):
        return LocBuilder()

    def all(self):
        res = _facade.api_request(LOG_ALL, None)
        if res.status == ResStatus.eOk:
            res.data = unpack(res.data, LoggerInfoList).items
        return res


def log():
    """Return the console log emission entry; the caller location is captured here.

    返回主控制台发射入口，调用行位置在此捕获；仅限单表达式内联使用，禁止缓存复用。
    """
    frame = sys._getframe(1)
    return LogBuilder((frame.f_code.co_filename, frame.f_lineno, frame.f_code.co_name))


def log_mgr():
    """Return the log management entry.

    返回日志管理域链式入口。
    """
    return LogMgrBuilder()