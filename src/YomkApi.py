try:
    from YomkServerPyVersion import __version__
except ImportError:  # 版本模块缺失时保持门面 import 链可用
    __version__ = "unknown"

from YomkServerPy import (
    YomkServer,
    YomkService,
    YomkResponse,
    ResStatus,
    Context,
    ContextKeyInfo,
    CheckStatus,
    ContextChecker,
    ContextMonitor,
    ContextGroupKey,
    ContextGroupInfo,
    Function,
    CallFunction,
    EventLoopPkg,
    EventLoopQuery,
    EventLoopInfo,
    Event,
    ConsoleLogProxy,
    FileLogProxy,
    Log,
    LogFile,
    FuncInfo,
    LoggerQuery,
    LoggerInfo,
    LoggerState,
    ServerServiceInfo,
    _yomk_err_pos_log,
    _yomk_source_loc_prefix
)
import YomkServerPy as _yomk_core  # 位置前缀三开关经模块属性读写
from typing import Any, Callable, Optional
import atexit
import logging
import sys
import threading

from abc import ABC, abstractmethod

_logger = logging.getLogger(__name__)  # 模块内部诊断 logger（log 名让位给发射域链式入口）


class YomkBoot(ABC):
    def __init__(self) -> None:
        pass

    @abstractmethod
    def before(self) -> int:
        pass

    @abstractmethod
    def start(self) -> int:
        pass

    @abstractmethod
    def after(self) -> int:
        pass


# 可空化：未 init 时为 None
g_server: Optional[YomkServer] = None
_init_done: bool = False       # 进程级幂等标志
_init_lock = threading.Lock()  # init/shutdown 互斥


def _api_request(url, pkg):
    # 统一门面请求入口：未 init 记日志并返回 eInvalid
    global g_server
    if g_server is None:
        _logger.error("YomkServer is not init")
        return YomkResponse(ResStatus.eInvalid, "YomkServer is not init")
    return g_server.request(url, pkg)


def version():
    # 获取框架版本号
    return __version__


def init(async_thread_count=0):
    # 初始化框架单例并启动内置服务，async_thread_count 为异步请求线程池大小（0 表示自动检测）；
    # 进程级幂等：重复调用返回既有实例；shutdown 后不再重建
    global g_server, _init_done
    with _init_lock:
        if not _init_done:
            srv = YomkServer.create(async_thread_count)
            srv.start_service(
                ["/YomkFunctionPool", "/YomkContext", "/YomkEventLoop",
                 "/YomkLogger", "/YomkServerInfo"])
            g_server = srv
            _init_done = True
            # 进程退出兜底销毁
            atexit.register(shutdown)
    return g_server


def server():
    global g_server
    return g_server


def server_instance():
    # 获取全局单例，未初始化返回 None
    global g_server
    return g_server


def shutdown():
    # 关闭服务器并释放所有服务；未 init 直接返回
    global g_server
    with _init_lock:
        if g_server is None:
            return
        g_server.shutdown()
        g_server = None


# ==================== 链式主面（对齐 C++ 六域 builder，113 方法位）====================
# 域入口：server_api()/ctx()/fn_pool()/event_loop()/log()/log_mgr()。
# server_api() 为 server 域链式入口（对齐 C++ yomk::server()）；因扁平 server() 尚被
# 消费方以单例语义占用，闭环 5 删除扁平 server() 后本入口更名为 server()。
# async_ 为 C++ async() 的 Python 关键字规避名（PEP 8 尾下划线惯例）。
# builder 全程仅限单表达式内联链式使用，禁止缓存复用（对齐 C++ 警告）。


def _chain_converge_status(res):
    # 请求域 .status() 三态收敛：非成功记框架自诊断（对齐 C++ YOMK_DIAG_ERROR）
    if res.status != ResStatus.Succeeded:
        _yomk_err_pos_log(res.msg if res.msg else "request failed")
    return res.status


def _chain_log_content(loc, args):
    # 发射正文 = 位置前缀（进程级三开关）+ 逐参追加（str() 流式化、bool → true/false、
    # 单空格分隔；前缀为空时首参不加前导空格，对齐 C++ buildLogContent/appendLogArg）
    msg = _yomk_source_loc_prefix(*loc)
    for arg in args:
        s = "true" if arg is True else ("false" if arg is False else str(arg))
        msg = msg + " " + s if msg else s
    return msg


class AsyncRequestBuilder:
    # 异步分面：server 域 .async_(cb) 之后继续 .msg() 并以 .request() 无返回终结
    def __init__(self, url, callback):
        self._url = url
        self._callback = callback
        self._msg = None

    def msg(self, m):
        self._msg = m
        return self

    def request(self):
        # 异步终结无返回（对齐 C++ void）；未初始化记日志
        if g_server is None:
            _logger.error("YomkServer is not init")
            return None
        return g_server.async_request(self._url, self._msg, self._callback)


class ServerBuilder:
    # server 请求域 builder：url/msg 累积 + request()/status() 收敛 + 服务编排终结
    def __init__(self):
        self._url = ""
        self._msg = None
        self._async_thread_count = 0
        self._srv_cls = None
        self._srv_name = ""
        self._srv = None
        self._boot = None
        self._fn_url = ""

    def url(self, u):
        self._url = u
        return self

    def msg(self, m):
        self._msg = m
        return self

    def request(self):
        # 同步请求终结，返回 YomkResponse
        return _api_request(self._url, self._msg)

    def status(self):
        # 写终结三态收敛（失败记诊断），返回 ResStatus
        return _chain_converge_status(self.request())

    def async_(self, func=None):
        # 切入异步分面：已累积消息携带进分面
        ab = AsyncRequestBuilder(self._url, func)
        ab._msg = self._msg
        return ab

    def version(self):
        # 框架版本号
        return __version__

    def async_thread(self, n):
        # 累积异步线程池大小（init 用）
        self._async_thread_count = n
        return self

    def init(self):
        # 初始化框架单例（进程级幂等，shutdown 后不再重建）
        init(self._async_thread_count)

    def instance(self):
        # 全局单例（未初始化 None）
        return g_server

    def shutdown(self):
        # 关闭服务器并释放所有服务
        shutdown()

    def srv(self, cls, name=""):
        # 累积待创建服务类与名（create 用）
        self._srv_cls = cls
        self._srv_name = name
        return self

    def create(self):
        # 创建并注册服务，返回 0/-1
        if g_server is None:
            _logger.error("YomkServer is not init")
            return -1
        return g_server.new_service(self._srv_cls, self._srv_name)

    def srv_name(self, n):
        self._srv_name = n
        return self

    def srv_ptr(self, s):
        self._srv = s
        return self

    def add(self):
        # 注册已构造服务，返回 0/-1
        if g_server is None:
            _logger.error("YomkServer is not init")
            return -1
        if not self._srv:
            _logger.error("YomkService is null")
            return -1
        if self._srv_name:
            self._srv.set_name(self._srv_name)
        return g_server.add_service(self._srv)

    def remove(self):
        # 按名称注销服务，返回 0/-1
        if g_server is None:
            _logger.error("YomkServer is not init")
            return -1
        if not self._srv_name:
            _logger.error("service name is empty")
            return -1
        return g_server.del_service(self._srv_name)

    def boot_ptr(self, b):
        self._boot = b
        return self

    def run(self):
        # 执行启动编排（before -> start -> after），返回 0/钩子错误码
        return boot(self._boot)

    def services(self):
        # 查询所有服务名称
        return _api_request("/YomkServerInfo/services", None)

    def functions(self):
        # 查询指定服务的功能函数清单（用 srv_name 累积）
        return _api_request("/YomkServerInfo/functions", self._srv_name)

    def fn_url(self, u):
        self._fn_url = u
        return self

    def get_info(self):
        # 查询指定功能函数元信息（用 fn_url 累积）
        return _api_request("/YomkServerInfo/function", self._fn_url)

    def all(self):
        # 查询框架全量服务及功能函数拓扑
        return _api_request("/YomkServerInfo/all", None)


class GroupSwitch:
    # 组级开关分面：ctx().group(g).checker()/monitor() 之后仅 enable()/disable()
    def __init__(self, group, monitor):
        self._group = group
        self._monitor = monitor  # False = 校验门控，True = 变更监听

    def enable(self):
        url = "/YomkContext/turn_on_monitor" if self._monitor else "/YomkContext/turn_on_checker"
        return _chain_converge_status(_api_request(url, self._group))

    def disable(self):
        url = "/YomkContext/turn_off_monitor" if self._monitor else "/YomkContext/turn_off_checker"
        return _chain_converge_status(_api_request(url, self._group))


class ContextValueBuilder:
    # 读路径分面：ctx().key(n).group(g).msg() 之后 with_default(d).get() 直取值本体
    def __init__(self, key, group):
        self._key = key
        self._group = group
        self._default = None

    def with_default(self, d):
        self._default = d
        return self

    def get(self):
        # 未命中/失败回退 default（未初始化同），对齐 C++ ContextValue::get
        if g_server is None:
            return self._default
        res = g_server.request("/YomkContext/get", Context(self._key, self._default, self._group))
        if res.status == ResStatus.eOk:
            return res.data
        _logger.error("get context failed")
        return self._default


class ContextBuilder:
    # context 域 builder：key/group/value 累积 + 写终结三态收敛 + 读终结 Response
    def __init__(self):
        self._key = ""
        self._group = "main"
        self._value = None
        self._cb_kind = None  # "checker" / "monitor" / None（后调覆盖先调）
        self._checker = None
        self._monitor = None
        self._async_monitor = False

    def key(self, n):
        self._key = n
        return self

    def group(self, g="main"):
        self._group = g
        return self

    def value(self, v):
        self._value = v
        return self

    def create(self):
        return _chain_converge_status(
            _api_request("/YomkContext/create", Context(self._key, self._value, self._group)))

    def set(self):
        return _chain_converge_status(
            _api_request("/YomkContext/set", Context(self._key, self._value, self._group)))

    def destroy(self):
        return _chain_converge_status(
            _api_request("/YomkContext/destroy", ContextGroupKey(self._group, self._key)))

    def checker_fn(self, fn):
        self._checker = fn
        self._cb_kind = "checker"
        return self

    def monitor_fn(self, fn):
        self._monitor = fn
        self._cb_kind = "monitor"
        return self

    def async_(self, on=True):
        # 累积监听通知形态（monitor_fn 之后使用），缺省同步
        self._async_monitor = on
        return self

    def apply(self):
        # 键级回调终结：按累积种类派发（后调覆盖先调）；未累积返回 Invalid 并记诊断
        if self._cb_kind == "checker":
            return _chain_converge_status(_api_request(
                "/YomkContext/set_checker", ContextChecker(self._key, self._checker, self._group)))
        if self._cb_kind == "monitor":
            if self._async_monitor:
                # 异步形态：同步位 no-op 占位，异步位承载用户回调
                pkg = ContextMonitor(self._key, lambda c: None, self._monitor, self._group)
            else:
                pkg = ContextMonitor(self._key, self._monitor, False, self._group)
            return _chain_converge_status(_api_request("/YomkContext/set_monitor", pkg))
        _yomk_err_pos_log("apply: no checker/monitor accumulated")
        return ResStatus.Invalid

    def checker(self):
        # 组级校验门控开关分面
        return GroupSwitch(self._group, False)

    def monitor(self):
        # 组级变更监听开关分面
        return GroupSwitch(self._group, True)

    def keys(self):
        return _api_request("/YomkContext/keys", self._group)

    def info(self):
        return _api_request("/YomkContext/key", ContextGroupKey(self._group, self._key))

    def groups(self):
        return _api_request("/YomkContext/groups", None)

    def all(self):
        return _api_request("/YomkContext/all", None)

    def msg(self, cls=None):
        # 切入读路径分面（cls 对齐 C++ msg<T>() 类型指定位，S13 豁免：get 不做类型校验）
        return ContextValueBuilder(self._key, self._group)


class FnPoolBuilder:
    # function_pool 域 builder：fn_name/fn/msg_name/description/msg 累积 + 终结
    def __init__(self):
        self._name = ""
        self._fn = None
        self._msg_name = ""
        self._description = ""
        self._pkg = None

    def fn_name(self, n):
        self._name = n
        return self

    def fn(self, f):
        self._fn = f
        return self

    def msg_name(self, m):
        self._msg_name = m
        return self

    def description(self, d):
        self._description = d
        return self

    def msg(self, data):
        self._pkg = data
        return self

    def install(self):
        return _chain_converge_status(_api_request(
            "/YomkFunctionPool/register",
            Function(self._name, self._fn, self._msg_name, self._description)))

    def uninstall(self):
        return _chain_converge_status(_api_request("/YomkFunctionPool/unregister", self._name))

    def call(self):
        # 阻塞调用已注册函数
        return _api_request("/YomkFunctionPool/call", CallFunction(self._name, self._pkg))

    def list(self):
        return _api_request("/YomkFunctionPool/names", None)

    def get_info(self):
        # 查询单个已注册函数元信息（用 fn_name 累积）
        return _api_request("/YomkFunctionPool/name", self._name)

    def all(self):
        return _api_request("/YomkFunctionPool/all", None)


class EventLoopBuilder:
    # event_loop 域 builder：loop 分面定位 + 参数前置累积 + 无参终结
    def __init__(self):
        self._name = ""
        self._fn = None
        self._msg_name = ""
        self._data = None
        self._tag = ""
        self._tag_count = 3

    def loop(self, n):
        self._name = n
        return self

    def handle_fn(self, f):
        self._fn = f
        return self

    def msg_name(self, m):
        self._msg_name = m
        return self

    def msg(self, data):
        self._data = data
        return self

    def tag(self, t):
        self._tag = t
        return self

    def tag_count(self, n):
        self._tag_count = n
        return self

    def start(self):
        # 同名循环不重建：复用既有实例，本次累积的 handle_fn/msg_name 不生效
        return _chain_converge_status(
            _api_request("/YomkEventLoop/start", EventLoopPkg(self._name, self._fn, self._msg_name)))

    def stop(self):
        return _chain_converge_status(_api_request("/YomkEventLoop/stop", self._name))

    def post(self):
        # 异步投递，返回入队三态
        return _chain_converge_status(
            _api_request("/YomkEventLoop/post", Event(self._name, self._data, self._fn, self._tag)))

    def post_wait(self):
        # 同步投递（阻塞等待执行完成）
        return _chain_converge_status(
            _api_request("/YomkEventLoop/post_wait", Event(self._name, self._data, self._fn, self._tag)))

    def destroy(self):
        return _chain_converge_status(_api_request("/YomkEventLoop/destroy", self._name))

    def list(self):
        return _api_request("/YomkEventLoop/loops", None)

    def get_info(self):
        return _api_request("/YomkEventLoop/loop", EventLoopQuery(self._name, self._tag_count))

    def all(self):
        return _api_request("/YomkEventLoop/all", None)


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
        return _chain_converge_status(_api_request(
            "/YomkLogger/file_log",
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def warn(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/file_log",
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def error(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/file_log",
            Log(Log.eError, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def debug(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/file_log",
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def trace(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/file_log",
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._sink, self._tag)))

    def fatal(self, *args):
        return _chain_converge_status(_api_request(
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
        return _chain_converge_status(_api_request(
            "/YomkLogger/console_log",
            Log(Log.eInfo, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def warn(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/console_log",
            Log(Log.eWarn, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def error(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/console_log",
            Log(Log.eError, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def debug(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/console_log",
            Log(Log.eDebug, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def trace(self, *args):
        return _chain_converge_status(_api_request(
            "/YomkLogger/console_log",
            Log(Log.eTrace, _chain_log_content(self._loc, args), self._tag, self._tag)))

    def fatal(self, *args):
        return _chain_converge_status(_api_request(
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
        return _chain_converge_status(_api_request(
            "/YomkLogger/on_console_log_by_level", Log(self._level, "", self._name)))

    def disable(self):
        return _chain_converge_status(_api_request(
            "/YomkLogger/off_console_log_by_level", Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        # None = 恢复默认输出
        return _chain_converge_status(_api_request(
            "/YomkLogger/set_console_log_proxy", ConsoleLogProxy(self._proxy)))

    def list(self):
        return _api_request("/YomkLogger/loggers", "console")

    def get_info(self):
        return _api_request("/YomkLogger/logger", LoggerQuery(self._name, "console"))

    def status(self):
        # 全局主闸状态（4 级别 + proxy，与文件侧主闸相互独立）
        return _api_request("/YomkLogger/console_global_state", None)


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
        return _chain_converge_status(_api_request(
            "/YomkLogger/on_file_log_by_level", Log(self._level, "", self._name)))

    def disable(self):
        return _chain_converge_status(_api_request(
            "/YomkLogger/off_file_log_by_level", Log(self._level, "", self._name)))

    def proxy(self, fn):
        self._proxy = fn
        return self

    def apply(self):
        return _chain_converge_status(_api_request(
            "/YomkLogger/set_file_log_proxy", FileLogProxy(self._proxy)))

    def list(self):
        return _api_request("/YomkLogger/loggers", "file")

    def get_info(self):
        return _api_request("/YomkLogger/logger", LoggerQuery(self._name, "file"))

    def status(self):
        return _api_request("/YomkLogger/file_global_state", None)

    def create(self):
        # 创建文件日志器（dir 累积自 log_mgr().dir()）
        return _chain_converge_status(_api_request(
            "/YomkLogger/create_file_logger", LogFile(self._name, self._dir)))

    def flush(self):
        return _chain_converge_status(_api_request("/YomkLogger/write_file_log", self._name))

    def remove(self):
        return _chain_converge_status(_api_request("/YomkLogger/delete_logger", self._name))


class LocBuilder:
    # 位置前缀三开关分面：构造承接当前进程状态，apply 一次性落三开关（恒 Succeeded）
    def __init__(self):
        self._show_file = _yomk_core._yomk_loc_show_file
        self._show_line = _yomk_core._yomk_loc_show_line
        self._show_func = _yomk_core._yomk_loc_show_func

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
        _yomk_core._yomk_loc_show_file = self._show_file
        _yomk_core._yomk_loc_show_line = self._show_line
        _yomk_core._yomk_loc_show_func = self._show_func
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
        return _api_request("/YomkLogger/all", None)


def server_api():
    # server 域链式入口（对齐 C++ yomk::server()）；扁平 server() 尚被消费方以
    # 单例语义占用，闭环 5 删除扁平 server() 后本入口更名为 server()
    return ServerBuilder()


def ctx():
    # context 域链式入口（对齐 C++ yomk::ctx()）
    return ContextBuilder()


def fn_pool():
    # function_pool 域链式入口（对齐 C++ yomk::fn_pool()）
    return FnPoolBuilder()


def event_loop():
    # event_loop 域链式入口（对齐 C++ yomk::event_loop()）
    return EventLoopBuilder()


def log():
    # log 发射域链式入口（对齐 C++ yomk::log()）；调用行位置在此捕获，
    # 仅限单表达式内联使用，禁止缓存复用 Builder
    frame = sys._getframe(1)
    return LogBuilder((frame.f_code.co_filename, frame.f_lineno, frame.f_code.co_name))


def log_mgr():
    # log 管理域链式入口（对齐 C++ yomk::log_mgr()）
    return LogMgrBuilder()


# ==================== 旧扁平门面（deprecated，闭环 5 消费方迁移后删除）====================

def new_service(class_name: type[YomkService], name: str = ""):
    global g_server
    if g_server is None:
        _logger.error("YomkServer is not init")
        return -1
    # 服务创建、命名与 init 内聚到 server 层
    return g_server.new_service(class_name, name)


def add_service(srv: YomkService, name: str = ""):
    global g_server
    if g_server is None:
        _logger.error("YomkServer is not init")
        return -1
    if not srv:
        _logger.error("YomkService is null")
        return -1
    if name:
        srv.set_name(name)
    return g_server.add_service(srv)


def del_service(name: str = ""):
    # 按名称注销服务
    global g_server
    if g_server is None:
        _logger.error("YomkServer is not init")
        return -1
    if not name:
        _logger.error("service name is empty")
        return -1
    return g_server.del_service(name)


def boot(boot: Optional[YomkBoot] = None):
    # 初始化并执行启动编排流程（before -> start -> after）；
    # 钩子未显式返回（None）等价成功 0
    init()
    if boot is None:
        return 0

    ret = boot.before()
    if ret is not None and ret != 0:
        _logger.error("YomkBoot before failed! ")
        return ret

    ret = boot.start()
    if ret is not None and ret != 0:
        _logger.error("YomkBoot start failed! ")
        return ret

    ret = boot.after()
    if ret is not None and ret != 0:
        _logger.error("YomkBoot after failed! ")
        return ret

    return 0


def async_request(url: str, pkg: Any, callback: Callable[[YomkResponse], None]) -> None:
    global g_server
    if g_server is None:
        _logger.error("YomkServer is not init")
        return
    return g_server.async_request(url, pkg, callback)


def request(url: str, pkg: Any) -> YomkResponse:
    return _api_request(url, pkg)


def _yomk_api_file_line_msg(caller_frame, *args):
    # 内容拼装：内容头为调用处行号，
    # 其后逐 args 以空格连接；非 str args 经 str() 流式化（对齐 ostringstream <<）
    msg = "[" + str(caller_frame.f_lineno) + "]"
    for arg in args:
        msg += " " + str(arg)
    return msg


# ==================== LOG_API ====================

def set_console_log_proxy(console_log_proxy_func):
    # 设置控制台日志代理回调（传 None 恢复默认输出，取消代理回调）
    return _api_request("/YomkLogger/set_console_log_proxy",
                        ConsoleLogProxy(console_log_proxy_func))


def set_file_log_proxy(file_log_proxy_func):
    # 设置文件日志代理回调（传 None 恢复默认落盘，取消代理回调；与控制台代理相互独立）
    return _api_request("/YomkLogger/set_file_log_proxy",
                        FileLogProxy(file_log_proxy_func))


def console_log_info_tag(tag, *args):
    # 控制台日志输出（tag 同填日志器名与标签）
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eInfo, _yomk_api_file_line_msg(sys._getframe(1), *args), tag, tag))


def console_log_warn_tag(tag, *args):
    # 控制台警告日志输出
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eWarn, _yomk_api_file_line_msg(sys._getframe(1), *args), tag, tag))


def console_log_error_tag(tag, *args):
    # 控制台错误日志输出
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eError, _yomk_api_file_line_msg(sys._getframe(1), *args), tag, tag))


def console_log_debug_tag(tag, *args):
    # 控制台调试日志输出
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eDebug, _yomk_api_file_line_msg(sys._getframe(1), *args), tag, tag))


# ---- 无 tag 便捷入口----
# 直接实现而非转发 *_tag 版：sys._getframe(1) 取本函数调用方帧，保证 [行号] 为用户调用行
#

def console_log_info(*args):
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eInfo, _yomk_api_file_line_msg(sys._getframe(1), *args), "MainLogger", "MainLogger"))


def console_log_warn(*args):
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eWarn, _yomk_api_file_line_msg(sys._getframe(1), *args), "MainLogger", "MainLogger"))


def console_log_error(*args):
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eError, _yomk_api_file_line_msg(sys._getframe(1), *args), "MainLogger", "MainLogger"))


def console_log_debug(*args):
    return _api_request("/YomkLogger/console_log",
                        Log(Log.eDebug, _yomk_api_file_line_msg(sys._getframe(1), *args), "MainLogger", "MainLogger"))


def file_log_create(log_dir, log_file):
    # 创建文件日志器（log_dir 目录、log_file 日志器名）
    return _api_request("/YomkLogger/create_file_logger", LogFile(log_file, log_dir))


def file_log_write(log_file):
    # 将指定日志器的缓冲落盘
    return _api_request("/YomkLogger/write_file_log", log_file)


def file_log_info_tag(log_file, tag, *args):
    # 文件日志输出（logger=log_file、tag=tag）
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eInfo, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, tag))


def file_log_warn_tag(log_file, tag, *args):
    # 文件警告日志输出
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eWarn, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, tag))


def file_log_error_tag(log_file, tag, *args):
    # 文件错误日志输出
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eError, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, tag))


def file_log_debug_tag(log_file, tag, *args):
    # 文件调试日志输出
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eDebug, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, tag))


# ---- 文件日志无 tag 便捷入口----

def file_log_info(log_file, *args):
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eInfo, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, "MainLogger"))


def file_log_warn(log_file, *args):
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eWarn, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, "MainLogger"))


def file_log_error(log_file, *args):
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eError, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, "MainLogger"))


def file_log_debug(log_file, *args):
    return _api_request("/YomkLogger/file_log",
                        Log(Log.eDebug, _yomk_api_file_line_msg(sys._getframe(1), *args), log_file, "MainLogger"))


def on_console_log_info(logger_name=""):
    # 开启控制台 Info 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_console_log_by_level",
                        Log(Log.eInfo, "", logger_name))


def on_console_log_warn(logger_name=""):
    # 开启控制台 Warn 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_console_log_by_level",
                        Log(Log.eWarn, "", logger_name))


def on_console_log_error(logger_name=""):
    # 开启控制台 Error 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_console_log_by_level",
                        Log(Log.eError, "", logger_name))


def on_console_log_debug(logger_name=""):
    # 开启控制台 Debug 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_console_log_by_level",
                        Log(Log.eDebug, "", logger_name))


def off_console_log_info(logger_name=""):
    # 关闭控制台 Info 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_console_log_by_level",
                        Log(Log.eInfo, "", logger_name))


def off_console_log_warn(logger_name=""):
    # 关闭控制台 Warn 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_console_log_by_level",
                        Log(Log.eWarn, "", logger_name))


def off_console_log_error(logger_name=""):
    # 关闭控制台 Error 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_console_log_by_level",
                        Log(Log.eError, "", logger_name))


def off_console_log_debug(logger_name=""):
    # 关闭控制台 Debug 级别日志（logger_name 空 = 全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_console_log_by_level",
                        Log(Log.eDebug, "", logger_name))


def on_file_log_info(logger_name=""):
    # 开启文件 Info 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_file_log_by_level",
                        Log(Log.eInfo, "", logger_name))


def on_file_log_warn(logger_name=""):
    # 开启文件 Warn 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_file_log_by_level",
                        Log(Log.eWarn, "", logger_name))


def on_file_log_error(logger_name=""):
    # 开启文件 Error 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_file_log_by_level",
                        Log(Log.eError, "", logger_name))


def on_file_log_debug(logger_name=""):
    # 开启文件 Debug 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/on_file_log_by_level",
                        Log(Log.eDebug, "", logger_name))


def off_file_log_info(logger_name=""):
    # 关闭文件 Info 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_file_log_by_level",
                        Log(Log.eInfo, "", logger_name))


def off_file_log_warn(logger_name=""):
    # 关闭文件 Warn 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_file_log_by_level",
                        Log(Log.eWarn, "", logger_name))


def off_file_log_error(logger_name=""):
    # 关闭文件 Error 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_file_log_by_level",
                        Log(Log.eError, "", logger_name))


def off_file_log_debug(logger_name=""):
    # 关闭文件 Debug 级别日志（logger_name 空 = 文件全局主闸，与无参语义一致；具名 = 逐器门控）
    return _api_request("/YomkLogger/off_file_log_by_level",
                        Log(Log.eDebug, "", logger_name))


def logger_info_loggers(logger_type):
    # 按类型获取日志器名列表（type: "console"/"file"，仅返回该类型的纯名）
    return _api_request("/YomkLogger/loggers", logger_type)


def logger_info_logger(logger_name, logger_type):
    # 查询指定日志器状态（type: "console"/"file"；两表允许同名，须按类型精确单查）
    return _api_request("/YomkLogger/logger", LoggerQuery(logger_name, logger_type))


def logger_info_all():
    # 查询全部日志器及逐器门控状态（命中 msg 恒 "ok"，data 为 list[LoggerInfo] 清单，
    # 每条内嵌 logger_state 状态段（LoggerState：console/file 条目各自为对应侧逐器
    # 门控层真值，proxy_on 各报对应侧代理态））
    return _api_request("/YomkLogger/all", None)


def logger_info_global(type="console"):
    # 查询全局主闸状态（4 级别 + proxy；type: "console"/"file"，两套主闸/两套代理
    # 各自独立供两层归因；缺省 "console" 与历史无参语义一致，未知 type 返回 eNo）
    if type == "file":
        return _api_request("/YomkLogger/file_global_state", None)
    if type == "console":
        return _api_request("/YomkLogger/console_global_state", None)
    return YomkResponse(ResStatus.eNo, "unknown logger type.")


def file_log_delete(logger_name):
    # 删除指定日志器（console 表计数 + file 表删除触发析构落盘）
    return _api_request("/YomkLogger/delete_logger", logger_name)


# Context 各 API 的组名约定：组名永远是最后一个参数，缺省 "main"（空组名返回 eNo
# "group name is empty"；非 main 组首次寻址即自动创建，惰性、不支持销毁、组名唯一）

def context_create(key: str, value: Any, group_name: str = "main") -> YomkResponse:
    ctx = Context(key, value, group_name)
    return _api_request("/YomkContext/create", ctx)


def context_destroy(key: str, group_name: str = "main") -> YomkResponse:
    return _api_request("/YomkContext/destroy", ContextGroupKey(group_name, key))


def context_get(key: str, default_value: Any, group_name: str = "main") -> Any:
    global g_server
    if g_server is None:
        return default_value
    ctx = Context(key, default_value, group_name)
    res = g_server.request("/YomkContext/get", ctx)
    if res.status == ResStatus.eOk:
        return res.data
    # eOk 才取值，失败返回默认值
    _logger.error("get context failed")
    return default_value


def context_set(key: str, value: Any, group_name: str = "main") -> YomkResponse:
    ctx = Context(key, value, group_name)
    return _api_request("/YomkContext/set", ctx)


def context_turn_on_checker(group_name: str = "main") -> YomkResponse:
    return _api_request("/YomkContext/turn_on_checker", group_name)


def context_turn_off_checker(group_name: str = "main") -> YomkResponse:
    return _api_request("/YomkContext/turn_off_checker", group_name)


def context_set_checker(key: str, checker: Callable[[Context], CheckStatus],
                        group_name: str = "main") -> YomkResponse:
    contextChecker = ContextChecker(key, checker, group_name)
    return _api_request("/YomkContext/set_checker", contextChecker)


def context_turn_on_monitor(group_name: str = "main") -> YomkResponse:
    return _api_request("/YomkContext/turn_on_monitor", group_name)


def context_turn_off_monitor(group_name: str = "main") -> YomkResponse:
    return _api_request("/YomkContext/turn_off_monitor", group_name)


def context_set_monitor(key: str, monitor: Callable[[Context], None], async_monitor: bool = False,
                        group_name: str = "main") -> YomkResponse:
    # async_monitor=True 时用户回调投递到异步位、同步位以 no-op 占位
    # （服务端为两函数位形态，同步位空函数仅占位，
    # 因 async 位存在即走异步路径，占位函数不会被执行）
    if async_monitor:
        contextMonitor = ContextMonitor(
            key, lambda c: None, monitor, group_name)
    else:
        contextMonitor = ContextMonitor(key, monitor, False, group_name)
    return _api_request("/YomkContext/set_monitor", contextMonitor)


def context_info_keys(group_name: str = "main") -> YomkResponse:
    # 获取组内全部上下文键名（缺省 main 组）
    return _api_request("/YomkContext/keys", group_name)


def context_info_key(key: str, group_name: str = "main") -> YomkResponse:
    # 查询指定上下文键状态（含所属组 group 字段）
    return _api_request("/YomkContext/key", ContextGroupKey(group_name, key))


def context_info_groups() -> YomkResponse:
    # 查询全部组名清单（命中 msg 恒 "ok"，data 为组名列表：main 恒在首位）
    return _api_request("/YomkContext/groups", None)


def context_info_all() -> YomkResponse:
    # 查询全部组及其全部键的内省元信息（跨组聚合：命中 msg 恒 "ok"，data 为 ContextGroupInfo
    # 列表，层级为组 → 键元信息；main 组条目恒在首位，自定义组按字典序追加）
    return _api_request("/YomkContext/all", None)


def function_pool_register(name: str, func: Callable[[Any], YomkResponse], msg_name: str = "",
                           description: str = "") -> YomkResponse:
    # 注册函数到全局函数池：msg_name 声明期望消息类型，description 函数说明，均可为空（仅内省元数据）
    return _api_request("/YomkFunctionPool/register", Function(name, func, msg_name, description))


def function_pool_unregister(name: str) -> YomkResponse:
    # 从函数池注销函数
    return _api_request("/YomkFunctionPool/unregister", name)


def function_pool_call(name: str, pkg: Any) -> YomkResponse:
    return _api_request("/YomkFunctionPool/call", CallFunction(name, pkg))


def function_pool_info_names() -> YomkResponse:
    # 获取函数池中全部函数名
    return _api_request("/YomkFunctionPool/names", None)


def function_pool_info_name(name: str) -> YomkResponse:
    # 查询单个已注册函数元信息
    return _api_request("/YomkFunctionPool/name", name)


def function_pool_info_all() -> YomkResponse:
    # 查询全部已注册函数元信息（结构化列表，命中 msg 恒 "ok"，data 为 FuncInfo 列表，总数由列表长度承载）
    return _api_request("/YomkFunctionPool/all", None)


def event_loop_start(event_loop_name: str, callback: Optional[Callable[[Any], YomkResponse]] = None, msg_name: str = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/start", EventLoopPkg(event_loop_name, callback, msg_name))


def event_loop_stop(event_loop_name: str) -> YomkResponse:
    return _api_request("/YomkEventLoop/stop", event_loop_name)


def event_loop_destroy(event_loop_name: str) -> YomkResponse:
    return _api_request("/YomkEventLoop/destroy", event_loop_name)


def event_loop_post(event_loop_name: str, pkg: Any, callback: Optional[Callable[[Any], YomkResponse]] = None, tag: str = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/post", Event(event_loop_name, pkg, callback, tag))


def event_loop_post_wait(event_loop_name: str, pkg: Any, callback: Optional[Callable[[Any], YomkResponse]] = None, tag: str = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/post_wait", Event(event_loop_name, pkg, callback, tag))


def event_loop_info_loops() -> YomkResponse:
    # 获取全部事件循环名称列表
    return _api_request("/YomkEventLoop/loops", None)


def event_loop_info_loop(event_loop_name: str, tag_count: Optional[int] = None) -> YomkResponse:
    # 内省：单个事件循环（结构化 EventLoopQuery：名称字面量精确匹配 + 采样数）
    if tag_count is not None:
        return _api_request("/YomkEventLoop/loop", EventLoopQuery(event_loop_name, tag_count))
    return _api_request("/YomkEventLoop/loop", EventLoopQuery(event_loop_name))


def event_loop_info_all() -> YomkResponse:
    # 查询全部事件循环元信息（结构化列表，命中 msg 恒 "ok"，data 为 EventLoopInfo 列表，next_tags 取 3 条）
    return _api_request("/YomkEventLoop/all", None)


def server_info_services() -> YomkResponse:
    # 查询所有服务名称
    return _api_request("/YomkServerInfo/services", None)


def server_info_functions(srv_name: str) -> YomkResponse:
    # 查询指定服务的功能函数清单（结构化列表，命中 msg 恒 "ok"，data 为 FuncInfo
    # 列表与 /function 单查、/all 聚合同族；不存在服务返回空表）
    return _api_request("/YomkServerInfo/functions", srv_name)


def server_info_function(url: str) -> YomkResponse:
    # 查询指定功能函数元信息
    return _api_request("/YomkServerInfo/function", url)


def server_info_all() -> YomkResponse:
    # 查询框架全量服务及功能函数拓扑（结构化层级列表：每服务一个 ServerServiceInfo
    # 条目 service_name + funcs（FuncInfo 列表，与单查同族），命中 msg 恒 "ok"）
    return _api_request("/YomkServerInfo/all", None)
