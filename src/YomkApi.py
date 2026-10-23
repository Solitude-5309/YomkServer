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
    Function,
    CallFunction,
    EventLoopPkg,
    EventLoopInfo,
    Event,
    ConsoleLogProxy,
    Log,
    LogFile,
    FuncInfo,
    LoggerInfo
)
from typing import Any, Callable, Optional
import atexit
import logging
import sys
import threading

from abc import ABC, abstractmethod

log = logging.getLogger(__name__)


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
        log.error("YomkServer is not init")
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


def new_service(class_name: type[YomkService], name: str = ""):
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return -1
    # 服务创建、命名与 init 内聚到 server 层
    return g_server.new_service(class_name, name)


def add_service(srv: YomkService, name: str = ""):
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return -1
    if not srv:
        log.error("YomkService is null")
        return -1
    if name:
        srv.set_name(name)
    return g_server.add_service(srv)


def del_service(name: str = ""):
    # 按名称注销服务
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return -1
    if not name:
        log.error("service name is empty")
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
        log.error("YomkBoot before failed! ")
        return ret

    ret = boot.start()
    if ret is not None and ret != 0:
        log.error("YomkBoot start failed! ")
        return ret

    ret = boot.after()
    if ret is not None and ret != 0:
        log.error("YomkBoot after failed! ")
        return ret

    return 0


def async_request(url: str, pkg: Any, callback: Callable[[YomkResponse], None]) -> None:
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
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


def on_console_log_info():
    # 开启控制台 Info 级别日志
    return _api_request("/YomkLogger/on_console_log_by_level", Log(Log.eInfo))


def on_console_log_warn():
    # 开启控制台 Warn 级别日志
    return _api_request("/YomkLogger/on_console_log_by_level", Log(Log.eWarn))


def on_console_log_error():
    # 开启控制台 Error 级别日志
    return _api_request("/YomkLogger/on_console_log_by_level", Log(Log.eError))


def on_console_log_debug():
    # 开启控制台 Debug 级别日志
    return _api_request("/YomkLogger/on_console_log_by_level", Log(Log.eDebug))


def off_console_log_info():
    # 关闭控制台 Info 级别日志
    return _api_request("/YomkLogger/off_console_log_by_level", Log(Log.eInfo))


def off_console_log_warn():
    # 关闭控制台 Warn 级别日志
    return _api_request("/YomkLogger/off_console_log_by_level", Log(Log.eWarn))


def off_console_log_error():
    # 关闭控制台 Error 级别日志
    return _api_request("/YomkLogger/off_console_log_by_level", Log(Log.eError))


def off_console_log_debug():
    # 关闭控制台 Debug 级别日志
    return _api_request("/YomkLogger/off_console_log_by_level", Log(Log.eDebug))


def logger_info_loggers():
    # 获取全部日志器列表
    return _api_request("/YomkLogger/loggers", None)


def logger_info_logger(logger_name):
    # 查询指定日志器状态
    return _api_request("/YomkLogger/logger", logger_name)


def logger_info_all():
    # 查询全部日志器及开关配置状态
    return _api_request("/YomkLogger/all", None)


def file_log_delete(logger_name):
    # 删除指定日志器（console 表计数 + file 表删除触发析构落盘）
    return _api_request("/YomkLogger/delete_logger", logger_name)


def context_create(key: str, value: Any) -> YomkResponse:
    ctx = Context(key, value)
    return _api_request("/YomkContext/create", ctx)


def context_destroy(key: str) -> YomkResponse:
    return _api_request("/YomkContext/destroy", key)


def context_get(key: str, default_value: Any) -> Any:
    global g_server
    if g_server is None:
        return default_value
    ctx = Context(key, default_value)
    res = g_server.request("/YomkContext/get", ctx)
    if res.status == ResStatus.eOk:
        return res.data
    # eOk 才取值，失败返回默认值
    log.error("get context failed")
    return default_value


def context_set(key: str, default_value: Any) -> YomkResponse:
    ctx = Context(key, default_value)
    return _api_request("/YomkContext/set", ctx)


def context_turn_on_checker() -> YomkResponse:
    return _api_request("/YomkContext/turn_on_checker", None)


def context_turn_off_checker() -> YomkResponse:
    return _api_request("/YomkContext/turn_off_checker", None)


def context_set_checker(key: str, checker: Callable[[Context], CheckStatus]) -> YomkResponse:
    contextChecker = ContextChecker(key, checker)
    return _api_request("/YomkContext/set_checker", contextChecker)


def context_turn_on_monitor() -> YomkResponse:
    return _api_request("/YomkContext/turn_on_monitor", None)


def context_turn_off_monitor() -> YomkResponse:
    return _api_request("/YomkContext/turn_off_monitor", None)


def context_set_monitor(key: str, monitor: Callable[[Context], None], async_monitor: bool = False) -> YomkResponse:
    # async_monitor=True 时用户回调投递到异步位、同步位以 no-op 占位
    # （服务端为两函数位形态，同步位空函数仅占位，
    # 因 async 位存在即走异步路径，占位函数不会被执行）
    if async_monitor:
        contextMonitor = ContextMonitor(key, lambda c: None, monitor)
    else:
        contextMonitor = ContextMonitor(key, monitor, False)
    return _api_request("/YomkContext/set_monitor", contextMonitor)


def context_info_keys() -> YomkResponse:
    # 获取全部上下文键名
    return _api_request("/YomkContext/keys", None)


def context_info_key(key: str) -> YomkResponse:
    # 查询指定上下文键状态
    return _api_request("/YomkContext/key", key)


def context_info_all() -> YomkResponse:
    # 查询全部上下文键元信息（结构化列表，命中 msg 恒 "ok"，data 为 ContextKeyInfo 列表）
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
    # 查询指定事件循环状态与事件积压数
    if tag_count is not None:
        return _api_request("/YomkEventLoop/loop", event_loop_name + " " + str(tag_count))
    return _api_request("/YomkEventLoop/loop", event_loop_name)


def event_loop_info_all() -> YomkResponse:
    # 查询全部事件循环元信息（结构化列表，命中 msg 恒 "ok"，data 为 EventLoopInfo 列表，next_tags 取 3 条）
    return _api_request("/YomkEventLoop/all", None)


def server_info_services() -> YomkResponse:
    # 查询所有服务名称
    return _api_request("/YomkServerInfo/services", None)


def server_info_functions(srv_name: str) -> YomkResponse:
    # 查询指定服务的全部功能函数
    return _api_request("/YomkServerInfo/functions", srv_name)


def server_info_function(url: str) -> YomkResponse:
    # 查询指定功能函数元信息
    return _api_request("/YomkServerInfo/function", url)


def server_info_all() -> YomkResponse:
    # 查询框架全量服务及功能函数拓扑
    return _api_request("/YomkServerInfo/all", None)
