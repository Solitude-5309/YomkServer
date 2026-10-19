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
    CheckStatus,
    ContextChecker,
    ContextMonitor,
    Function,
    CallFunction,
    EventLoopPkg,
    Event,
    YomkFuncInfo
)
from typing import Any, Callable, Optional
import logging
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


# 可空化：未 init 时为 None（对齐 main ServerHolder.server 初始 nullptr）
g_server: Optional[YomkServer] = None
_init_done: bool = False       # 进程级幂等标志（对齐 main std::call_once）
_init_lock = threading.Lock()  # init/shutdown 互斥（对齐 main mtx()）


def _api_request(url, pkg):
    # 统一门面请求入口：未 init 记日志并返回 eInvalid（对齐 main YOMK_API_REQUIRE_SERVER）
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return YomkResponse(ResStatus.eInvalid, "YomkServer is not init")
    return g_server.request(url, pkg)


def version():
    # 获取框架版本号（对齐 main VERSION_API）
    return __version__


def init(async_thread_count=0):
    # 初始化框架单例并启动内置服务，async_thread_count 为异步请求线程池大小（0 表示自动检测）；
    # 进程级幂等：重复调用返回既有实例；shutdown 后不再重建（对齐 main once_flag 已消耗）
    global g_server, _init_done
    with _init_lock:
        if not _init_done:
            srv = YomkServer.create(async_thread_count)
            srv.start_service(
                ["/YomkContext", "/YomkFunctionPool", "/YomkEventLoop"])
            g_server = srv
            _init_done = True
    return g_server


def server():
    global g_server
    return g_server


def server_instance():
    # 获取全局单例，未初始化返回 None（对齐 main serverInstance）
    global g_server
    return g_server


def shutdown():
    # 关闭服务器并释放所有服务；未 init 直接返回（对齐 main setServer(nullptr)）
    global g_server
    with _init_lock:
        if g_server is None:
            return
        g_server.shutdown()
        g_server = None


def new_service(class_name: type[YomkService], name: type[str] = ""):
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return -1
    # 服务创建、命名与 init 内聚到 server 层（对齐 main YOMK_NEW_SERVICE 链路）
    return g_server.new_service(class_name, name)


def add_service(srv: type[YomkService], name: type[str] = ""):
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


def del_service(name: type[str] = ""):
    # 按名称注销服务（对齐 main delService：空名拒绝）
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return -1
    if not name:
        log.error("service name is empty")
        return -1
    return g_server.del_service(name)


def boot(boot: type[YomkBoot] = None):
    # 初始化并执行启动编排流程（before -> start -> after）；
    # 钩子未显式返回（None）等价成功 0（对齐 main 返回 int 0）
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


def async_request(url: type[str], pkg: type[Any], callback: type[Callable[[YomkResponse], None]]) -> None:
    global g_server
    if g_server is None:
        log.error("YomkServer is not init")
        return
    return g_server.async_request(url, pkg, callback)


def request(url: type[str], pkg: type[Any]) -> YomkResponse:
    return _api_request(url, pkg)


def context_create(key: type[str], value: type[Any]) -> YomkResponse:
    ctx = Context(key, value)
    return _api_request("/YomkContext/create", ctx)


def context_destroy(key: type[str]) -> YomkResponse:
    return _api_request("/YomkContext/destroy", key)


def context_get(key: type[str], default_value: type[Any]) -> Any:
    global g_server
    if g_server is None:
        return default_value
    ctx = Context(key, default_value)
    res = g_server.request("/YomkContext/get", ctx)
    if res.status == ResStatus.eOk:
        return res.data
    # eOk 才取值，失败返回默认值（对齐 main CONTEXT_GET 语义）
    log.error("get context failed")
    return default_value


def context_set(key: type[str], default_value: type[Any]) -> YomkResponse:
    ctx = Context(key, default_value)
    return _api_request("/YomkContext/set", ctx)


def context_turn_on_checker() -> YomkResponse:
    return _api_request("/YomkContext/turn_on_checker", None)


def context_turn_off_checker() -> YomkResponse:
    return _api_request("/YomkContext/turn_off_checker", None)


def context_set_checker(key: type[str], checker: type[Callable[[Context], CheckStatus]]) -> YomkResponse:
    contextChecker = ContextChecker(key, checker)
    return _api_request("/YomkContext/set_checker", contextChecker)


def context_turn_on_monitor() -> YomkResponse:
    return _api_request("/YomkContext/turn_on_monitor", None)


def context_turn_off_monitor() -> YomkResponse:
    return _api_request("/YomkContext/turn_off_monitor", None)


def context_set_monitor(key: type[str], monitor: type[Callable[[Context], None]], async_monitor: type[bool] = False) -> YomkResponse:
    # async_monitor=True 时用户回调投递到异步位、同步位以 no-op 占位（对齐 main
    # 单函数 + bool 标志的门面语义；服务端为两函数位形态，同步位空函数仅占位，
    # 因 async 位存在即走异步路径，占位函数不会被执行）
    if async_monitor:
        contextMonitor = ContextMonitor(key, lambda c: None, monitor)
    else:
        contextMonitor = ContextMonitor(key, monitor, False)
    return _api_request("/YomkContext/set_monitor", contextMonitor)


def context_info_keys() -> YomkResponse:
    # 获取全部上下文键名（对齐 main CONTEXT_INFO_KEYS；服务端功能模块轮补齐）
    return _api_request("/YomkContext/keys", None)


def context_info_key(key: type[str]) -> YomkResponse:
    # 查询指定上下文键状态（对齐 main CONTEXT_INFO_KEY）
    return _api_request("/YomkContext/key", key)


def context_info_all() -> YomkResponse:
    # 查询全部上下文键及其数据类型（对齐 main CONTEXT_INFO_ALL）
    return _api_request("/YomkContext/all", None)


def function_pool_register(name: type[str], func: type[Callable[[Any], YomkResponse]], msg_name: type[str] = "") -> YomkResponse:
    return _api_request("/YomkFunctionPool/register", Function(name, func, msg_name))


def function_pool_unregister(name: type[str]) -> YomkResponse:
    # 从函数池注销函数（对齐 main FUNCTIONPOOL_UNREGISTER）
    return _api_request("/YomkFunctionPool/unregister", name)


def function_pool_call(name: type[str], pkg: type[Any]) -> YomkResponse:
    return _api_request("/YomkFunctionPool/call", CallFunction(name, pkg))


def function_pool_info_names() -> YomkResponse:
    # 获取函数池中全部函数名（对齐 main FUNCTIONPOOL_INFO_NAMES）
    return _api_request("/YomkFunctionPool/names", None)


def function_pool_info_name(name: type[str]) -> YomkResponse:
    # 查询单个已注册函数元信息（对齐 main FUNCTIONPOOL_INFO_NAME）
    return _api_request("/YomkFunctionPool/name", name)


def function_pool_info_all() -> YomkResponse:
    # 查询全部已注册函数元信息（对齐 main FUNCTIONPOOL_INFO_ALL）
    return _api_request("/YomkFunctionPool/all", None)


def event_loop_start(event_loop_name: type[str], callback: type[Callable[[Any], YomkResponse]] = None, msg_name: type[str] = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/start", EventLoopPkg(event_loop_name, callback, msg_name))


def event_loop_stop(event_loop_name: type[str]) -> YomkResponse:
    return _api_request("/YomkEventLoop/stop", event_loop_name)


def event_loop_destroy(event_loop_name: type[str]) -> YomkResponse:
    return _api_request("/YomkEventLoop/destroy", event_loop_name)


def event_loop_post(event_loop_name: type[str], pkg: type[Any], callback: type[Callable[[Any], YomkResponse]] = None, tag: type[str] = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/post", Event(event_loop_name, pkg, callback, tag))


def event_loop_post_wait(event_loop_name: type[str], pkg: type[Any], callback: type[Callable[[Any], YomkResponse]] = None, tag: type[str] = "") -> YomkResponse:
    return _api_request("/YomkEventLoop/post_wait", Event(event_loop_name, pkg, callback, tag))


def event_loop_info_loops() -> YomkResponse:
    # 获取全部事件循环名称列表（对齐 main EVENTLOOP_INFO_LOOPS）
    return _api_request("/YomkEventLoop/loops", None)


def event_loop_info_loop(event_loop_name: type[str], tag_count: type[int] = None) -> YomkResponse:
    # 查询指定事件循环状态与事件积压数（对齐 main EVENTLOOP_INFO_LOOP 双重载）
    if tag_count is not None:
        return _api_request("/YomkEventLoop/loop", event_loop_name + " " + str(tag_count))
    return _api_request("/YomkEventLoop/loop", event_loop_name)


def event_loop_info_all() -> YomkResponse:
    # 查询全部事件循环状态（对齐 main EVENTLOOP_INFO_ALL）
    return _api_request("/YomkEventLoop/all", None)
