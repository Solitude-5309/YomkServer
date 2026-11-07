from typing import Any, Callable, Dict, List
from enum import Enum


class ResStatus(Enum):
    eInvalid: int = -1
    eOk: int = 0
    eNo: int = 1
    # C++ 口径别名（YomkPayload.h ResStatus：Invalid=-1/Succeeded=0/Failed=1），
    # 与 eInvalid/eOk/eNo 逐一同值；新代码一律用 C++ 口径，eOk/eNo 保留向后兼容
    Succeeded: int = 0
    Failed: int = 1
    Invalid: int = -1


class CheckStatus(Enum):
    eAccept: int = 0
    eReject: int = 1


class YomkResponse:
    status: ResStatus   # 本次请求的状态
    msg: str            # 对状态的说明（不携带返回数据）
    data: Any           # 携带的返回数据

    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None,
    ) -> None: ...


class YomkService:
    name: str
    server: Any
    functions: Dict[str, Callable[[Any], YomkResponse]]
    function_infos: Dict[str, FuncInfo]
    registered: bool
    # 注销标志（对齐 main std::atomic<bool> m_deleted）：del_service/同名替换置位，
    # shutdown 不置位——删除即停（weak_func 判活叠加此标志），排空期回调照常执行
    m_deleted: bool
    rwlock_functions: Any

    def __init__(self, server: Any) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    def mark_registered(self) -> None: ...
    def mark_deleted(self) -> None: ...
    def deleted(self) -> bool: ...
    def weak_func(self, func: Callable[..., Any], discard: str = "response") -> Callable[..., Any]: ...
    def init(self) -> None: ...
    def deinit(self) -> None: ...
    def install_func(self, name: str, function: Callable[[Any], YomkResponse],
                     msg_name: str = "", description: str = "") -> None: ...
    def func_infos(self) -> Dict[str, FuncInfo]: ...
    def invoke(self, name: str, pkg: Any) -> Any: ...
    def request(self, url: str, pkg: Any = None) -> Any: ...
    def async_request(self, url: str, pkg: Any = None,
                      callback: Callable[[Any], None] = None) -> None: ...


class YomkServer:
    services: Dict[str, YomkService]
    rwlock_services: Any
    shutdown_done: bool
    executor: Any

    def __init__(self, async_thread_count: int = 0) -> None: ...
    @classmethod
    def create(cls, async_thread_count: int = 0) -> "YomkServer": ...
    def start_service(self, srv_names: List[str]) -> int: ...
    def new_service(self, srv_cls: type, srv_name: str = "") -> int: ...
    def add_service(self, service: YomkService) -> int: ...
    def del_service(self, srv_name: str) -> int: ...
    def shutdown(self) -> None: ...
    def service_names(self) -> List[str]: ...
    def service_func_infos(self, srv_name: str) -> Dict[str, FuncInfo]: ...
    def request(self, url: str, pkg: Any = None) -> Any: ...
    def async_request(self, url: str, pkg: Any = None,
                      callback: Callable[[Any], None] = None) -> None: ...


class Context:
    key: str
    value: Any
    group: str
    def __init__(self, key: str = "", value: Any = None,
                 group: str = "main") -> None: ...


class ContextKeyInfo:
    # Context 键内省元信息（/YomkContext/key 单查返回数据；同时是 ContextGroupInfo.keys 清单元素）
    key: str                    # 键名
    type_name: str              # 值类型名（值为 None 时为空串）
    checker_on: bool            # 门控是否开启
    monitor_count: int          # monitor 总数
    async_monitor_count: int    # 其中 async monitor 数
    group: str                  # key 所属组名

    def __init__(self, key: str = "", type_name: str = "",
                 checker_on: bool = False, monitor_count: int = 0,
                 async_monitor_count: int = 0, group: str = "main") -> None: ...


class ContextChecker:
    key: str
    check_func: Callable[[Any], CheckStatus]
    group: str
    def __init__(self, key: str = "", check_func: Callable[[Any], CheckStatus] = None,
                 group: str = "main") -> None: ...


class ContextMonitor:
    key: str
    monitor_func: Callable[[Any], None]
    async_monitor_func: bool
    group: str

    def __init__(self, key: str = "", monitor_func: Callable[[Any], None] = None,
                 async_monitor_func: bool = False,
                 group: str = "main") -> None: ...


class ContextGroupKey:
    # Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
    group: str
    key: str
    def __init__(self, group: str = "main", key: str = "") -> None: ...


class ContextGroupInfo:
    # Context 组级内省条目（/YomkContext/all 返回数据元素：组名 + 该组下全部键元信息）
    group_name: str
    keys: List[ContextKeyInfo]
    def __init__(self, group_name: str = "",
                 keys: List[ContextKeyInfo] = None) -> None: ...


class Function:
    name: str
    func: Callable[[Any], YomkResponse]
    msg_name: str
    description: str

    def __init__(self, name: str, func: Callable[[Any], YomkResponse],
                 msg_name: str = "", description: str = "") -> None: ...


class CallFunction:
    name: str
    pkg: Any
    def __init__(self, name: str, pkg: Any) -> None: ...


class Event:
    m_eventLoopName: str
    m_pkg: Any
    m_serviceFunc: Callable[[Any], YomkResponse]
    m_eventId: int
    m_response: Any
    m_waitCallback: Callable[[], None]
    m_tag: str

    def __init__(
        self,
        event_loop_name: str = "",
        pkg: Any = None,
        func: Callable[[Any], YomkResponse] = None,
        tag: str = ""
    ) -> None: ...
    def handle(self) -> None: ...


class EventLoopPkg:
    m_eventloopName: str
    m_defaultServiceFunc: Callable[[Any], YomkResponse]
    m_msgName: str

    def __init__(
        self,
        event_loop_name: str = "",
        default_service_func: Callable[[Any], YomkResponse] = None,
        msg_name: str = ""
    ) -> None: ...


class EventLoopQuery:
    # 事件循环单查载荷（/YomkEventLoop/loop 专用，对齐 C++ YomkPayload.h EventLoopQuery）：
    # 名称 + 队列头 tag 采样数，类型化字段无解析歧义
    m_loopName: str  # 循环名（按字面量精确匹配，不做任何切分）
    m_tagCount: int  # 队列头采样条数（缺省 3，与全量内省缺省一致）

    def __init__(self, loop_name: str = "", tag_count: int = 3) -> None: ...


class EventLoopInfo:
    # 事件循环内省元信息（/YomkEventLoop/loop 与 /YomkEventLoop/all 返回数据）
    loop_name: str                      # 循环名
    running: bool                       # worker 是否运行中
    pending: int                        # 积压事件数
    has_default_func: bool              # 是否注册默认处理函数
    msg_name: str                       # 默认处理函数期望消息类型名（可空）
    next_tags: List[str]                # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）

    def __init__(self, loop_name: str = "", running: bool = False,
                 pending: int = 0, has_default_func: bool = False,
                 msg_name: str = "", next_tags: List[str] = None) -> None: ...


class LogFile:
    # 文件日志器创建载荷（/YomkLogger/create_file_logger 请求数据）
    m_logger: str   # 日志器名
    m_dir: str      # 日志目录

    def __init__(self, logger: str = "", dir: str = "") -> None: ...


class Log:
    # 日志载荷（console_log/file_log/按级别开关端点共用）：ELogLevel 固定 int 底层类型，
    # 六级值序对齐 C++（Trace=0..Fatal=5）
    eTrace: int = 0
    eDebug: int = 1
    eInfo: int = 2
    eWarn: int = 3
    eError: int = 4
    eFatal: int = 5

    m_level: int     # 级别（六级常量之一）
    m_log: str       # 日志内容
    m_logger: str    # 日志器名：服务端按其查找/惰性创建日志器实例
    m_tag: str       # 日志标签：随记录显示 [tag] 段，区别于 m_logger

    def __init__(self, level: int = eInfo, log_msg: str = "",
                 logger: str = "", tag: str = "") -> None: ...


class ConsoleLogProxy:
    # 控制台日志代理回调载体（/YomkLogger/set_console_log_proxy 请求数据）
    m_consoleLogProxyFunc: Callable[[Log], bool]  # func(Log) -> bool，None 等价卸载

    def __init__(self, console_log_proxy_func: Callable[[Log], bool] = None) -> None: ...


class FileLogProxy:
    # 文件日志代理回调载体（/YomkLogger/set_file_log_proxy 请求数据；
    # 语义与控制台代理对称，两代理各自独立）
    m_fileLogProxyFunc: Callable[[Log], bool]  # func(Log) -> bool，None 等价卸载

    def __init__(self, file_log_proxy_func: Callable[[Log], bool] = None) -> None: ...


class LoggerQuery:
    # 日志器单查请求（/YomkLogger/logger 请求数据：名 + 类型，两表允许同名须按类型区分）
    m_logger_name: str
    m_type: str  # 类型："console" 或 "file"

    def __init__(self, logger_name: str = "", type: str = "") -> None: ...


class LoggerInfo:
    # 日志器内省元信息（/YomkLogger/logger 返回数据、/YomkLogger/all 清单段元素）
    logger_name: str  # 日志器名
    # 类型："console" 或 "file"（loggers 端点按此类型参数查询）
    type: str
    dir: str          # file 类型的日志目录（console 时为空串）
    # logger_state 状态段：console 条目承载控制台逐器门控层真值、file 条目承载
    # 文件逐器门控层真值（均为逐器层，全局主闸态经 console_global_state/
    # file_global_state 查询；proxy_on 各报对应侧代理态，两套代理各自独立）
    logger_state: LoggerState

    def __init__(self, logger_name: str = "", type: str = "",
                 dir: str = "", logger_state: LoggerState = None) -> None: ...


class LoggerState:
    # 日志器状态（trace/debug/info/warn/error/fatal/proxy 七开关，对齐 C++
    # YomkPayload.h LoggerState m_traceOn..m_fatalOn + m_proxyOn）：console 条目
    # 承载控制台逐器门控层真值，file 条目承载文件逐器门控层真值；两套主闸相互独立
    trace_on: bool    # trace 级别开关
    debug_on: bool    # debug 级别开关
    info_on: bool     # info 级别开关
    warn_on: bool     # warn 级别开关
    error_on: bool    # error 级别开关
    fatal_on: bool    # fatal 级别开关
    # proxy_on：代理已安装——/console_global_state 报控制台代理、
    # /file_global_state 报文件代理（两套代理各自独立）
    proxy_on: bool

    def __init__(self, trace_on: bool = False, debug_on: bool = False,
                 info_on: bool = False, warn_on: bool = False,
                 error_on: bool = False, fatal_on: bool = False,
                 proxy_on: bool = False) -> None: ...


class ConsoleLogger:
    # 控制台日志器（sink），六级 stdout 输出、空 tag 回退日志器名、
    # 非法级别降级 Info、按器级别门控（关断级别 log 入口直接丢弃）
    m_name: str
    m_mutex: Any
    # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
    # （逐器门控），第一层为 YomkLogger 全局主闸（经 /console_global_state
    # 内省），输出取两层 AND
    m_showTraceLog: bool
    m_showDebugLog: bool
    m_showInfoLog: bool
    m_showWarnLog: bool
    m_showErrorLog: bool
    m_showFatalLog: bool

    def __init__(self) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    # 按器级别门控：关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
    def set_level_on(self, log_level: int, on: bool) -> None: ...
    def is_level_on(self, log_level: int) -> bool: ...
    def log(self, log_level: int, tag: str, log_msg: str) -> None: ...


class FileLogger:
    # 文件日志器（sink），log 入内存缓冲、write 追加落盘清缓冲、
    # 析构自动 write 兜底；init 建目录与空文件、六级门控（关断级别不入缓冲）
    m_name: str
    m_dir: str
    # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
    # （逐器门控），第一层为 YomkLogger 文件全局主闸（经 /file_global_state
    # 内省），输出取两层 AND
    m_showTraceLog: bool
    m_showDebugLog: bool
    m_showInfoLog: bool
    m_showWarnLog: bool
    m_showErrorLog: bool
    m_showFatalLog: bool
    m_logBuffer: str
    m_logBufferMutex: Any

    def __init__(self) -> None: ...
    def __del__(self) -> None: ...
    def get_name(self) -> str: ...
    def set_name(self, name: str) -> None: ...
    def get_dir(self) -> str: ...
    def set_dir(self, dir: str) -> None: ...
    # 按器级别门控：关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
    def set_level_on(self, log_level: int, on: bool) -> None: ...
    def is_level_on(self, log_level: int) -> bool: ...
    # 建目录与空文件（异常捕获转 bool 不穿透）
    def init(self) -> bool: ...
    def log(self, log_level: int, tag: str, log_msg: str) -> None: ...
    def write(self) -> None: ...


class Monitor:
    monitor_func: Callable[[Any], None]
    async_monitor_func: bool
    def __init__(self, monitor_func: Callable[[Any], None] = None,
                 async_monitor_func: bool = False) -> None: ...


class FuncInfo:
    # 功能函数元信息（/YomkFunctionPool/name 与 /all、/YomkServerInfo/function
    # 单查与 /all 清单段返回数据），预留扩展（如安全性校验）
    func_name: str
    msg_name: str
    description: str
    def __init__(self, func_name: str = "", msg_name: str = "",
                 description: str = "") -> None: ...


class ServerServiceInfo:
    # 服务级内省条目（/YomkServerInfo/all 返回数据元素：服务名 + 该服务注册的函数元信息列表）
    service_name: str   # 服务名（带 / 前缀，如 "/FmtSrv"）
    # 函数元信息（与 /function 单查同族 FuncInfo，func_name 为带 / 前缀 url 段）
    funcs: List[FuncInfo]
    def __init__(self, service_name: str = "",
                 funcs: List[FuncInfo] = None) -> None: ...


class ContextEntry:
    # Context 单条目内部结构（ContextGroup 持有），与消息包 Context 区分，仅服务内部使用
    value: Any
    checker: Any          # check_func 或 None（可空）
    monitors: List[Any]   # [Monitor]

    def __init__(self, value: Any) -> None: ...


class ContextGroup:
    # Context 底层实例组：完整承载单组功能（键存储、checker/monitor 门控开关、
    # 自持单线程异步监控池），不继承 YomkService；YomkContext 按组名路由到
    # ContextGroup 实例（对齐 EventLoop 模块双层写法）
    m_name: str
    m_checkerEnabled: bool
    m_monitorEnabled: bool
    m_contexts: Dict[str, ContextEntry]   # {key: ContextEntry}（单条目结构）
    m_contextsMutex: Any
    # 构造即建监控池：固定单线程保事件顺序
    m_monitorPool: Any

    def __init__(self, name: str) -> None: ...
    def name(self) -> str: ...
    # 排空式停止监控池（拒新 -> 排空 -> join）；shutdown 幂等，重复调用安全
    def stop(self) -> None: ...
    def create(self, key: str, value: Any) -> None: ...
    def destroy(self, key: str) -> None: ...
    def get(self, key: str, default_value: Any) -> Any: ...
    def set(self, key: str, value: Any) -> None: ...
    def turn_on_checker(self) -> None: ...
    def turn_off_checker(self) -> None: ...
    def turn_on_monitor(self) -> None: ...
    def turn_off_monitor(self) -> None: ...
    def set_checker(self, key: str, check_func: Callable[[Context], CheckStatus]) -> None: ...
    def set_monitor(self, key: str, monitor_func: Callable[[Context], None],
                    async_monitor: bool) -> None: ...
    def keys(self) -> List[str]: ...
    def key_info(self, key: str) -> ContextKeyInfo: ...
    def list_all(self) -> List[ContextKeyInfo]: ...


class YomkContext(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def deinit(self) -> None: ...
    def find_group(self, group_name: str) -> ContextGroup: ...
    def turn_on_monitor(self, pkg: Any) -> YomkResponse: ...
    def turn_off_monitor(self, pkg: Any) -> YomkResponse: ...
    def set_monitor(self, contextMonitor: ContextMonitor) -> YomkResponse: ...
    def set_checker(self, contextChecker: ContextChecker) -> YomkResponse: ...
    def turn_on_checker(self, pkg: Any) -> YomkResponse: ...
    def turn_off_checker(self, pkg: Any) -> YomkResponse: ...
    def create(self, ctx: Context) -> YomkResponse: ...
    def destroy(self, groupKey: ContextGroupKey) -> YomkResponse: ...
    def get(self, ctx: Context) -> YomkResponse: ...
    def set(self, ctx: Context) -> YomkResponse: ...
    def groups(self, pkg: Any) -> YomkResponse: ...
    def keys(self, pkg: Any) -> YomkResponse: ...
    def key_info(self, pkg: ContextGroupKey) -> YomkResponse: ...
    def list_all(self, pkg: Any) -> YomkResponse: ...


class YomkFunctionPool(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def registerFunction(self, function: Function) -> YomkResponse: ...
    def unRegisterFunction(self, func_name: str) -> YomkResponse: ...
    def callFunction(self, call_func: CallFunction) -> YomkResponse: ...
    def funcNames(self, pkg: Any) -> YomkResponse: ...
    def funcInfo(self, func_name: str) -> YomkResponse: ...
    def listAll(self, pkg: Any) -> YomkResponse: ...


class EventLoop:
    # 事件循环底层实例（非服务）：单 worker 线程消费事件队列，
    # YomkEventLoop 按循环名持有（对齐 Context 模块双层写法）
    m_event_queue: Any
    m_queueMutex: Any
    m_condition: Any
    m_thread: Any
    m_running: bool
    m_event_id: int
    m_defaultServiceFunc: Any
    m_defaultMsgName: str

    def __init__(self) -> None: ...
    # 双参 msg_name 同存，infoLine 内省依赖
    def set_default_service_func(self, func: Callable[[Any], YomkResponse],
                                 msg_name: str = "") -> None: ...
    def run(self) -> None: ...
    def start(self) -> None: ...
    def stop(self) -> int: ...
    def destroy(self) -> int: ...
    def __del__(self) -> None: ...
    def post(self, event: Event) -> int: ...
    def post_wait(self, event: Event) -> int: ...
    def info_snapshot(self, info: EventLoopInfo, tag_count: int) -> None: ...
    def info_line(self, loop_name: str, tag_count: int) -> EventLoopInfo: ...


class YomkEventLoop(YomkService):
    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def start(self, pkg: EventLoopPkg) -> YomkResponse: ...
    def stop(self, pkg: str) -> YomkResponse: ...
    def destroy(self, pkg: str) -> YomkResponse: ...
    def post(self, pkg: Event) -> YomkResponse: ...
    def post_wait(self, pkg: Event) -> YomkResponse: ...
    def loops(self, pkg: Any) -> YomkResponse: ...
    def loop_info(self, pkg: EventLoopQuery) -> YomkResponse: ...
    def list_all(self, pkg: Any) -> YomkResponse: ...
    def deinit(self) -> None: ...


class YomkLogger(YomkService):
    # 日志服务（/YomkLogger 16 端点）：控制台/文件两套日志器表 + 两套全局主闸
    # + 两套代理（相互独立），输出取主闸 × 逐器两层 AND
    m_consoleLoggers: Dict[str, ConsoleLogger]   # {"main": ConsoleLogger()} 起步
    m_consoleLoggersMutex: Any
    m_fileLoggers: Dict[str, FileLogger]
    m_fileLoggersMutex: Any
    # 控制台日志全局主闸（第一层）
    m_showConsoleTraceLog: bool
    m_showConsoleDebugLog: bool
    m_showConsoleInfoLog: bool
    m_showConsoleWarningLog: bool
    m_showConsoleErrorLog: bool
    m_showConsoleFatalLog: bool
    # 文件日志全局主闸（第一层）：与控制台主闸相互独立
    m_showFileTraceLog: bool
    m_showFileDebugLog: bool
    m_showFileInfoLog: bool
    m_showFileWarningLog: bool
    m_showFileErrorLog: bool
    m_showFileFatalLog: bool
    # proxy 字段专属锁：锁内拷贝快照、锁外调用回调，避免持锁重入；
    # 控制台与文件两套代理互不共锁，各自独立
    m_consoleLogProxyMutex: Any
    m_consoleLogProxy: bool
    m_consoleLogProxyFunc: Any
    # 文件日志全局代理（与控制台代理相互独立，语义对称）
    m_fileLogProxyMutex: Any
    m_fileLogProxy: bool
    m_fileLogProxyFunc: Any

    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def console_log(self, pkg: Log) -> YomkResponse: ...
    def set_console_log_proxy(self, pkg: ConsoleLogProxy) -> YomkResponse: ...
    def set_file_log_proxy(self, pkg: FileLogProxy) -> YomkResponse: ...
    def create_file_logger(self, pkg: LogFile) -> YomkResponse: ...
    def file_log(self, pkg: Log) -> YomkResponse: ...
    def write_file_log(self, pkg: str) -> YomkResponse: ...
    def delete_logger(self, pkg: str) -> YomkResponse: ...
    def off_console_log_by_level(self, pkg: Log) -> YomkResponse: ...
    def on_console_log_by_level(self, pkg: Log) -> YomkResponse: ...
    def set_named_console_level(self, logger_name: str, level: int, on: bool) -> YomkResponse: ...
    def off_file_log_by_level(self, pkg: Log) -> YomkResponse: ...
    def on_file_log_by_level(self, pkg: Log) -> YomkResponse: ...
    def set_named_file_level(self, logger_name: str, level: int, on: bool) -> YomkResponse: ...
    def loggers(self, pkg: str) -> YomkResponse: ...
    def current_console_state(self) -> LoggerState: ...
    def named_console_state(self, console_logger: ConsoleLogger,
                            proxy_on: bool) -> LoggerState: ...
    def current_file_state(self) -> LoggerState: ...
    def named_file_state(self, file_logger: FileLogger,
                         proxy_on: bool) -> LoggerState: ...
    def logger_info(self, pkg: LoggerQuery) -> YomkResponse: ...
    def list_all(self, pkg: Any) -> YomkResponse: ...
    def console_global_state(self, pkg: Any) -> YomkResponse: ...
    def file_global_state(self, pkg: Any) -> YomkResponse: ...


class YomkServerInfo(YomkService):
    # 服务信息内省服务：server 仅保留弱引用（weakref 判活契约），
    # server 已销毁时各端点统一走 destroyed 契约
    server: Any
    m_weakServer: Any

    def __init__(self, server: Any) -> None: ...
    def init(self) -> None: ...
    def list_services(self, pkg: Any) -> YomkResponse: ...
    def list_functions(self, pkg: str) -> YomkResponse: ...
    def function_info(self, pkg: str) -> YomkResponse: ...
    def list_all(self, pkg: Any) -> YomkResponse: ...
