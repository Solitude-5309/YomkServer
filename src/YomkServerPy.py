from readerwriterlock import rwlock
from enum import Enum
import logging
import os
import queue
import sys
import threading
import time
import weakref
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Callable

logging.basicConfig(
    format="[Yomk] [%(pathname)s:%(lineno)d] [%(funcName)s] %(message)s"
)

log = logging.getLogger(__name__)


class ResStatus(Enum):
    eInvalid = -1
    eOk = 0
    eNo = 1


class YomkResponse:
    def __init__(
        self,
        status: ResStatus = ResStatus.eInvalid,
        msg: str = "",
        data: Any = None,
    ):
        self.status = status  # 本次请求的状态
        self.msg = msg        # 对状态的说明（不携带返回数据）
        self.data = data      # 携带的返回数据


class YomkService:
    def __init__(self, server: "YomkServer"):
        self.name: str = ""
        self.server: "YomkServer" = server
        self.functions: dict[str, Callable[[Any], YomkResponse]] = {}
        self.function_infos: dict[str, FuncInfo] = {}
        self.registered: bool = False
        self.rwlock_functions = rwlock.RWLockFair()
        if not server:
            log.error("server is null")

    def get_name(self):
        return self.name

    def set_name(self, name):
        if not name:
            log.error("name is empty, set name failed, use default name")
            return
        if self.registered:
            log.error("service is registered, rename is not allowed -> " + name)
            return
        self.name = name

    def mark_registered(self):
        # 框架内部使用：服务入表注册后锁定服务名，此后 set_name 改名被拒绝
        self.registered = True

    def init(self):
        pass

    def deinit(self):
        pass

    def install_func(self, name, function, msg_name="", description=""):
        # msg_name 为期望消息类型元数据，description 为使用者自定义函数说明，均仅供内省
        if not name.startswith("/"):
            log.error("install function name is error: " +
                      name + ", please start with /")
            return

        if not function:
            log.error("install function is empty")
            return

        with self.rwlock_functions.gen_wlock():
            if (name in self.functions):
                log.error(
                    f"install function already exists -> {name}, update to current function")
            self.functions[name] = function
            if msg_name or description:
                self.function_infos[name] = FuncInfo(
                    name, msg_name, description)
            else:
                # 两参覆盖安装时清除残留的函数信息，保持内省与注册一致
                self.function_infos.pop(name, None)

    def func_infos(self):
        infos: dict[str, FuncInfo] = {}
        with self.rwlock_functions.gen_rlock():
            for func_name in self.functions:
                info = FuncInfo(func_name)
                stored = self.function_infos.get(func_name)
                if stored:
                    info.msg_name = stored.msg_name
                    info.description = stored.description
                infos[func_name] = info
        return infos

    def invoke(self, name, pkg):
        if not name or not name.startswith("/"):
            log.error("function name parse error: " + str(name) +
                      ", please start with /")
            return YomkResponse(
                ResStatus.eNo,
                "function name parse error: " +
                str(name) + ", please start with /"
            )
        with self.rwlock_functions.gen_rlock():
            if (name not in self.functions):
                log.error(
                    f"function not found -> {name}, please use YomkInstallFunc to install this function.")
                return YomkResponse(ResStatus.eNo, "function not found: " + name)
            func = self.functions[name]
        return func(pkg)

    def request(self, url, pkg=None):
        if not self.server:
            log.error("service is null, please check service")
            return YomkResponse(ResStatus.eNo, "service is null")
        return self.server.request(url, pkg)

    def async_request(self, url, pkg=None, callback=None):
        if not self.server:
            log.error("service is null, please check service")
            return
        self.server.async_request(url, pkg, callback)


class YomkServer:
    def __init__(self, async_thread_count=0):
        self.services: dict[str, YomkService] = {}
        self.rwlock_services = rwlock.RWLockFair()
        self.shutdown_done: bool = False
        if async_thread_count == 0:
            hw = os.cpu_count()
            # hw 为 0(或 None) 兜底 2，否则取一半向上取整
            async_thread_count = 2 if not hw else (hw + 1) // 2
        self.executor = ThreadPoolExecutor(async_thread_count)

    @classmethod
    def create(cls, async_thread_count=0):
        # 创建 YomkServer 实例，async_thread_count 为异步请求线程池大小（0 表示自动检测）
        return cls(async_thread_count)

    def start_service(self, srv_names):
        # 内置服务映射表（键名带 / 前缀）
        service_creators = {
            "/YomkContext": YomkContext,
            "/YomkFunctionPool": YomkFunctionPool,
            "/YomkEventLoop": YomkEventLoop,
            "/YomkLogger": YomkLogger,
            "/YomkServerInfo": YomkServerInfo,
        }
        for srv_name in srv_names:
            srv_cls = service_creators.get(srv_name)
            if srv_cls is None:
                log.error("yomk does not support service: " + srv_name)
                continue
            self.add_service(srv_cls(self))

        return 0

    def new_service(self, srv_cls, srv_name=""):
        # 创建并注册指定类型的服务
        srv = srv_cls(self)
        if srv_name:
            srv.set_name(srv_name)
        return self.add_service(srv)

    def add_service(self, service):
        if not isinstance(service, YomkService):
            log.error("service is null, please check the service.")
            return -1

        old_srv = None
        with self.rwlock_services.gen_wlock():
            if (service.get_name() in self.services):
                log.error(
                    f"service already exists -> {service.get_name()}, update to current service")
            old_srv = self.services.get(service.get_name())
            self.services[service.get_name()] = service
            # 入表即锁定服务名：注册后改名被拒绝，保证服务名与 service map 键一致
            service.mark_registered()

        # 锁外对旧服务 deinit：同名替换语义，替换前旧服务仍在表中
        if old_srv is not None:
            old_srv.deinit()

        # init 抛异常视同失败：捕获后走统一回滚，避免半初始化服务残留在表；
        # init 未显式返回（None）等价成功
        try:
            init_ret = service.init()
        except Exception as e:
            log.error("service init exception: " +
                      service.get_name() + ", what: " + str(e))
            init_ret = -1

        if init_ret is not None and init_ret != 0:
            log.error("service init error: " + service.get_name())
            self.del_service(service.get_name())
            return -1
        return 0

    def del_service(self, srv_name):
        srv = None
        with self.rwlock_services.gen_wlock():
            if (srv_name not in self.services):
                log.error("service not found, can not delete -> " + srv_name)
                return -1
            srv = self.services.pop(srv_name)

        # 锁外执行 deinit，与 shutdown 保持一致的锁外清理模式
        if srv is not None:
            srv.deinit()
        return 0

    def shutdown(self):
        # 幂等：重复调用直接返回，不会二次排空与 deinit
        if self.shutdown_done:
            return
        self.shutdown_done = True

        # 先停异步请求池（拒新 -> 排空）
        self.executor.shutdown(wait=True)

        with self.rwlock_services.gen_wlock():
            srvs = list(self.services.values())
            self.services.clear()

        # 锁外逐个 deinit，与 del_service 保持一致的锁外清理模式
        for srv in srvs:
            srv.deinit()

    def service_names(self):
        with self.rwlock_services.gen_rlock():
            return list(self.services.keys())

    def service_func_infos(self, srv_name):
        with self.rwlock_services.gen_rlock():
            srv = self.services.get(srv_name)
        if srv is None:
            log.error("service not found. " + srv_name +
                      ", please start the service.")
            return {}
        return srv.func_infos()

    def _parse_request_url(self, url):
        # 解析请求 url 为服务名与函数名（/ServiceName/func_name）；
        # 失败时返回 err_msg（供 request 构造响应，供 async_request 直接丢弃）
        if not url or url[0] != "/":
            return "", "", "url parse error: " + str(url) + ", please start with /"
        pos_end = url.find("/", 1)
        if pos_end == -1:
            return "", "", "url parse error: " + url + ", not found service name."
        srv_name = url[:pos_end]
        if not srv_name:
            return "", "", "url parse error: srv is empty. "
        func_name = url[pos_end:]
        if not func_name:
            return "", "", "url parse error: function name is empty"
        return srv_name, func_name, None

    def request(self, url, pkg=None):
        srv_name, func_name, err_msg = self._parse_request_url(url)
        if err_msg:
            log.error(err_msg)
            return YomkResponse(ResStatus.eNo, err_msg)

        with self.rwlock_services.gen_rlock():
            srv = self.services.get(srv_name)
        if srv is None:
            log.error("service not found. " + srv_name +
                      ", please start the service.")
            return YomkResponse(
                ResStatus.eNo,
                "service not found: " + srv_name
            )
        return srv.invoke(func_name, pkg)

    def async_request(self, url, pkg=None, callback=None):
        srv_name, func_name, err_msg = self._parse_request_url(url)
        if err_msg:
            log.error(err_msg)
            return

        # 服务器已关闭直接拒绝，池已停止时 submit 同样异常兜底
        if self.shutdown_done:
            log.error("server is shutting down, async request ignored.")
            return

        def task():
            try:
                # 执行时才查表；
                # 拼回完整 URL 走 request 的解析与查表路径
                result = self.request(srv_name + func_name, pkg)
                if callback:
                    callback(result)
            except Exception:
                log.exception("async task exception caught")

        try:
            self.executor.submit(task)
        except RuntimeError:
            log.error("server is shutting down, async request ignored.")

# msg


class Context:
    # key 所属组名随包携带（缺省 main；组名约定永远是 API 最后一个参数，下同）
    def __init__(self, key="", value=None, group="main"):
        self.key = key
        self.value = value
        self.group = group


class ContextKeyInfo:
    # Context 键内省元信息（/YomkContext/key 单查返回数据；同时是 ContextGroupInfo.keys 清单元素）
    def __init__(self, key="", type_name="", checker_on=False,
                 monitor_count=0, async_monitor_count=0, group="main"):
        self.key = key                    # 键名
        self.type_name = type_name        # 值类型名（值为 None 时为空串）
        self.checker_on = checker_on      # 门控是否开启
        self.monitor_count = monitor_count          # monitor 总数
        self.async_monitor_count = async_monitor_count  # 其中 async monitor 数
        self.group = group                # key 所属组名


class CheckStatus(Enum):
    eAccept = 0
    eReject = 1


class ContextChecker:
    def __init__(self, key="", check_func=None, group="main"):
        self.key = key
        self.check_func = check_func
        self.group = group                # 目标 key 所属组名（缺省 main）


class ContextMonitor:
    def __init__(self, key="", monitor_func=None, async_monitor_func=False, group="main"):
        self.key = key
        self.monitor_func = monitor_func
        self.async_monitor_func = async_monitor_func
        self.group = group                # 目标 key 所属组名（缺省 main）


class ContextGroupKey:
    # Context 组寻址载体（/destroy 与 /key 请求报文：定位某组下的某键，组名缺省 main）
    def __init__(self, group="main", key=""):
        self.group = group
        self.key = key


class ContextGroupInfo:
    # Context 组级内省条目（/YomkContext/all 返回数据元素：组名 + 该组下全部键元信息）
    def __init__(self, group_name="", keys=None):
        self.group_name = group_name      # 组名
        self.keys = keys if keys is not None else []  # 该组全部键的内省元信息（字典序）


class Function:
    def __init__(self, name, func, msg_name="", description=""):
        self.name = name
        self.func = func
        self.msg_name = msg_name
        # 函数说明，内容使用者自定义，与 msg_name 同为仅内省元数据
        self.description = description


class CallFunction:
    def __init__(self, name, pkg):
        self.name = name
        self.pkg = pkg


class Event:
    def __init__(self, event_loop_name="", pkg=None, func=None, tag=""):
        self.m_eventLoopName = event_loop_name
        self.m_pkg = pkg
        self.m_serviceFunc = func
        self.m_eventId = 0
        self.m_response = None
        self.m_waitCallback = None
        self.m_tag = tag

    def handle(self):
        if not self.m_serviceFunc:
            return
        self.m_response = self.m_serviceFunc(self.m_pkg)


class EventLoopPkg:
    def __init__(self, event_loop_name="", default_service_func=None, msg_name=""):
        self.m_eventloopName = event_loop_name
        self.m_defaultServiceFunc = default_service_func
        self.m_msgName = msg_name


class EventLoopInfo:
    # 事件循环内省元信息（/YomkEventLoop/loop 与 /YomkEventLoop/all 返回数据）
    def __init__(self, loop_name="", running=False, pending=0,
                 has_default_func=False, msg_name="", next_tags=None):
        self.loop_name = loop_name        # 循环名
        self.running = running            # worker 是否运行中
        self.pending = pending            # 积压事件数
        self.has_default_func = has_default_func  # 是否注册默认处理函数
        self.msg_name = msg_name          # 默认处理函数期望消息类型名（可空）
        # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）
        self.next_tags = next_tags if next_tags is not None else []


class LogFile:
    def __init__(self, logger="", dir=""):
        self.m_logger = logger
        self.m_dir = dir


class Log:
    # :Log：ELogLevel 固定 int 底层类型（eDebug=0/eInfo=1/eWarn=2/eError=3），
    # 非法值注入合法化（log 路径 default 降级 Info 分支可触达）
    eDebug = 0
    eInfo = 1
    eWarn = 2
    eError = 3

    def __init__(self, level=eInfo, log_msg="", logger="", tag=""):
        self.m_level = level
        self.m_log = log_msg
        self.m_logger = logger   # 日志器名：服务端按其查找/惰性创建日志器实例
        self.m_tag = tag         # 日志标签：随记录显示 [tag] 段，区别于 m_logger


class ConsoleLogProxy:
    def __init__(self, console_log_proxy_func=None):
        # func(Log) -> bool，None 等价卸载
        self.m_consoleLogProxyFunc = console_log_proxy_func


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
        # 状态段（成员 logger_state，类型 LoggerState，对应 C++ LoggerState m_loggerState）：
        # console 条目承载控制台逐器门控层真值（console 全局主闸态经
        # /console_global_state 查询）；file 条目承载文件逐器门控层真值
        # （file 全局主闸态经 /file_global_state 查询）
        self.logger_state = (logger_state if logger_state is not None
                             else LoggerState())


class LoggerState:
    # 日志器状态（debug/info/warn/error/proxy 五开关）：console 条目承载控制台
    # 逐器门控层真值，console 全局主闸态经 /console_global_state 查询；
    # file 条目承载文件逐器门控层真值，file 全局主闸态经 /file_global_state
    # 查询；两套主闸相互独立
    def __init__(self, debug_on=False, info_on=False, warn_on=False,
                 error_on=False, proxy_on=False):
        self.debug_on = debug_on        # debug 级别开关
        self.info_on = info_on          # info 级别开关
        self.warn_on = warn_on          # warn 级别开关
        self.error_on = error_on        # error 级别开关
        # proxy_on：代理已安装——/console_global_state 报控制台代理、
        # /file_global_state 报文件代理（文件代理落地前 file 侧恒 false）
        self.proxy_on = proxy_on


def _yomk_err_pos_log(msg):
    # 框架错误提示直写 stdout（可被测试捕获），带调用点位置前缀
    frame = sys._getframe(1)
    sys.stdout.write("[Yomk] [" + frame.f_code.co_filename + ":" + str(frame.f_lineno) + "] [" +
                     frame.f_code.co_name + "] " + msg + "\n")
    sys.stdout.flush()


def _yomk_log_local_time_formatted():
    # Logger 模块内部工具：
    # 本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]"
    now = time.time()
    lt = time.localtime(now)
    ms = int(round((now - int(now)) * 1000)) % 1000
    # python 的 tm_mon 已是 1-12 基，无需再加 1
    return "[%04d-%02d-%02d %02d:%02d:%02d.%03d]" % (
        lt.tm_year, lt.tm_mon, lt.tm_mday,
        lt.tm_hour, lt.tm_min, lt.tm_sec, ms)


class ConsoleLogger:
    # 控制台日志器，四级别 stdout 输出、空 tag 回退日志器名、
    # 非法级别降级 Info、按器级别门控（关断级别 log 入口直接丢弃）
    def __init__(self):
        self.m_name = "MainLogger"
        self.m_mutex = threading.Lock()
        # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
        # （逐器门控），第一层为 YomkLogger 全局主闸（经 /console_global_state
        # 内省），输出取两层 AND
        self.m_showDebugLog = True
        self.m_showInfoLog = True
        self.m_showWarnLog = True
        self.m_showErrorLog = True

    def get_name(self):
        return self.m_name

    def set_name(self, name):
        self.m_name = name

    def set_level_on(self, log_level, on):
        # 按器级别门控：关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
        if log_level == Log.eDebug:
            self.m_showDebugLog = on
        elif log_level == Log.eInfo:
            self.m_showInfoLog = on
        elif log_level == Log.eWarn:
            self.m_showWarnLog = on
        elif log_level == Log.eError:
            self.m_showErrorLog = on
        else:
            # unknown 级别按 Info 门控（与 log() 降级语义一致）
            self.m_showInfoLog = on

    def is_level_on(self, log_level):
        # 反查级别门控状态；unknown 级别返回 Info 门控值
        if log_level == Log.eDebug:
            return self.m_showDebugLog
        elif log_level == Log.eInfo:
            return self.m_showInfoLog
        elif log_level == Log.eWarn:
            return self.m_showWarnLog
        elif log_level == Log.eError:
            return self.m_showErrorLog
        else:
            return self.m_showInfoLog

    def log(self, log_level, tag, log_msg):
        # 级别门控在类级锁外判定（GIL 下 bool 读原子，无新锁）：关断级别直接丢弃
        # 不输出（unknown 级别按 Info 门控，与下方 default 降级语义一致）
        if log_level == Log.eDebug:
            if not self.m_showDebugLog:
                return
        elif log_level == Log.eInfo:
            if not self.m_showInfoLog:
                return
        elif log_level == Log.eWarn:
            if not self.m_showWarnLog:
                return
        elif log_level == Log.eError:
            if not self.m_showErrorLog:
                return
        else:
            if not self.m_showInfoLog:
                return

        time_str = _yomk_log_local_time_formatted()

        with self.m_mutex:
            # tag 优先显示；空 tag 回退日志器名，保持旧行为
            tag_shown = tag if tag else self.m_name
            if log_level == Log.eDebug:
                self._emit(time_str, "[Debug]", tag_shown, log_msg)
            elif log_level == Log.eInfo:
                self._emit(time_str, "[Info ]", tag_shown, log_msg)
            elif log_level == Log.eWarn:
                self._emit(time_str, "[Warn ]", tag_shown, log_msg)
            elif log_level == Log.eError:
                self._emit(time_str, "[Error]", tag_shown, log_msg)
            else:
                _yomk_err_pos_log("Unknown log level: " + str(log_level) +
                                  ", using Info level instead.")
                self._emit(time_str, "[Info ]", tag_shown, log_msg)

    @staticmethod
    def _emit(time_str, level_tag, tag_shown, log_msg):
        # 直写 stdout 并 flush
        sys.stdout.write(time_str + " " + level_tag +
                         " [" + tag_shown + "] " + log_msg + "\n")
        sys.stdout.flush()


class FileLogger:
    # 文件日志器，log 入内存缓冲、write 追加落盘清缓冲、
    # 析构自动 write 兜底；init 建目录与空文件（P2-b：异常捕获转 bool 不穿透）、
    # 按器级别门控（关断级别 log 入口直接丢弃不入缓冲）
    def __init__(self):
        self.m_name = "MainLogger"
        self.m_dir = ""
        # 级别门控随 sink 自持（每实例独立、默认全开）：两级主闸并联架构的第二层
        # （逐器门控），第一层为 YomkLogger 文件全局主闸（经 /file_global_state
        # 内省），输出取两层 AND
        self.m_showDebugLog = True
        self.m_showInfoLog = True
        self.m_showWarnLog = True
        self.m_showErrorLog = True
        self.m_logBuffer = ""
        self.m_logBufferMutex = threading.Lock()

    def __del__(self):
        # 析构兜底：缓冲未落盘内容不丢；
        # try 防护解释器关闭期属性缺失
        try:
            self.write()
        except Exception:
            pass

    def get_name(self):
        return self.m_name

    def set_name(self, name):
        self.m_name = name

    def get_dir(self):
        return self.m_dir

    def set_dir(self, dir):
        self.m_dir = dir

    def set_level_on(self, log_level, on):
        # 按器级别门控：关断的级别在 log() 入口直接丢弃；unknown 级别按 Info 门控
        if log_level == Log.eDebug:
            self.m_showDebugLog = on
        elif log_level == Log.eInfo:
            self.m_showInfoLog = on
        elif log_level == Log.eWarn:
            self.m_showWarnLog = on
        elif log_level == Log.eError:
            self.m_showErrorLog = on
        else:
            # unknown 级别按 Info 门控（与 log() 降级语义一致）
            self.m_showInfoLog = on

    def is_level_on(self, log_level):
        # 反查级别门控状态；unknown 级别返回 Info 门控值
        if log_level == Log.eDebug:
            return self.m_showDebugLog
        elif log_level == Log.eInfo:
            return self.m_showInfoLog
        elif log_level == Log.eWarn:
            return self.m_showWarnLog
        elif log_level == Log.eError:
            return self.m_showErrorLog
        else:
            return self.m_showInfoLog

    def init(self):
        log_file_path = self.m_dir + "/" + self.m_name + ".log"
        dir_path = os.path.dirname(log_file_path)
        try:
            if dir_path and not os.path.exists(dir_path):
                os.makedirs(dir_path)   # 对齐 create_directories（多级递归）
        except OSError as e:
            _yomk_err_pos_log("init file logger fs error: " +
                              log_file_path + ", " + str(e))
            return False
        try:
            with open(log_file_path, "w"):
                pass   # 建空文件（ofstream 默认 truncate 语义）
        except OSError:
            _yomk_err_pos_log("create log file failed: " + log_file_path)
            return False
        return True

    def log(self, log_level, tag, log_msg):
        # 级别门控在缓冲锁外判定（GIL 下 bool 读原子，无新锁）：关断级别直接丢弃
        # 不入缓冲（unknown 级别按 Info 门控，与下方 default 降级语义一致）；
        # 位置在取时间戳之前避免白算
        if log_level == Log.eDebug:
            if not self.m_showDebugLog:
                return
        elif log_level == Log.eInfo:
            if not self.m_showInfoLog:
                return
        elif log_level == Log.eWarn:
            if not self.m_showWarnLog:
                return
        elif log_level == Log.eError:
            if not self.m_showErrorLog:
                return
        else:
            if not self.m_showInfoLog:
                return

        time_str = _yomk_log_local_time_formatted()

        # tag 由日志器统一拼接：空 tag 不加前缀
        tag_prefix = ("[" + tag + "] ") if tag else ""

        with self.m_logBufferMutex:
            if log_level == Log.eDebug:
                self._append_line(time_str, tag_prefix, log_msg, " [Debug] ")
            elif log_level == Log.eInfo:
                self._append_line(time_str, tag_prefix, log_msg, " [Info ] ")
            elif log_level == Log.eWarn:
                self._append_line(time_str, tag_prefix, log_msg, " [Warn ] ")
            elif log_level == Log.eError:
                self._append_line(time_str, tag_prefix, log_msg, " [Error] ")
            else:
                _yomk_err_pos_log("Unknown log level: " + str(log_level) +
                                  ", using Info level instead.")
                self._append_line(time_str, tag_prefix, log_msg, " [Info ] ")

    def _append_line(self, time_str, tag_prefix, log_msg, level_tag):
        # 缓冲行格式时间戳 + " [级别] " + tag 前缀 + 内容 + 换行
        self.m_logBuffer += time_str + level_tag + tag_prefix + log_msg + "\n"

    def write(self):
        with self.m_logBufferMutex:
            # 空缓冲不触碰文件
            if not self.m_logBuffer:
                return
            try:
                with open(self.m_dir + "/" + self.m_name + ".log", "a") as log_file:
                    log_file.write(self.m_logBuffer)
            except OSError:
                _yomk_err_pos_log("open log file failed: " +
                                  self.m_dir + "/" + self.m_name + ".log")
                return
            self.m_logBuffer = ""


class Monitor:
    def __init__(self, monitor_func=None, async_monitor_func=False):
        self.monitor_func = monitor_func
        self.async_monitor_func = async_monitor_func


class FuncInfo:
    # 功能函数元信息（/YomkFunctionPool/name 与 /YomkFunctionPool/all、
    # /YomkServerInfo/function 单查与 /all 清单段返回数据），预留扩展（如安全性校验）
    def __init__(self, func_name="", msg_name="", description=""):
        self.func_name = func_name
        self.msg_name = msg_name
        self.description = description


class ServerServiceInfo:
    # 服务级内省条目（/YomkServerInfo/all 返回数据元素：服务名 + 该服务注册的函数元信息列表）
    def __init__(self, service_name="", funcs=None):
        self.service_name = service_name   # 服务名（带 / 前缀，如 "/FmtSrv"）
        # 函数元信息（与 /function 单查同族 FuncInfo，func_name 为带 / 前缀 url 段）
        self.funcs = funcs if funcs is not None else []

# modules


class ContextEntry:
    # Context 单条目内部结构（ContextGroup 持有），
    # 与消息包 Context 区分，仅服务内部使用
    def __init__(self, value):
        self.value = value
        self.checker = None    # check_func 或 None（可空）
        self.monitors = []     # [Monitor]


class ContextGroup:
    # Context 底层实例组：完整承载单组功能（键存储、checker/monitor 门控开关、
    # 自持单线程异步监控池），不继承 YomkService；YomkContext 按组名路由到
    # ContextGroup 实例（对齐 EventLoop 模块双层写法）
    def __init__(self, name):
        self.m_name = name
        self.m_checkerEnabled = False
        self.m_monitorEnabled = False
        self.m_contexts = {}     # {key: ContextEntry}（单条目结构）
        self.m_contextsMutex = rwlock.RWLockFair()
        # 构造即建监控池：池 shutdown 后不可复用，服务删除后重新注册经新对象新构造自然重建；
        # 固定单线程保事件顺序
        self.m_monitorPool = ThreadPoolExecutor(1)

    def name(self):
        return self.m_name

    def stop(self):
        # 排空式停止监控池（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
        # shutdown 幂等，重复调用安全
        if self.m_monitorPool is not None:
            self.m_monitorPool.shutdown(wait=True)
            self.m_monitorPool = None

    # ---- 明参业务方法：返回码约定见各方法注释，响应构造在 YomkContext 层 ----

    # 返回：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    def create(self, key, value):
        if not key:
            log.error("key is empty, please check Context.m_key.")
            return 1
        if value is None:
            log.error("value is empty, please check Context.m_value.")
            return 2

        with self.m_contextsMutex.gen_wlock():
            if key in self.m_contexts:
                log.error(
                    f"key already exists: {key}, please check Context.m_key.")
                return 3
            self.m_contexts[key] = ContextEntry(value)

        return 0

    # 返回：0 成功 / 1 key 不存在
    def destroy(self, key):
        with self.m_contextsMutex.gen_wlock():
            if key not in self.m_contexts:
                log.error(f"key is not exist: {key}, please check key.")
                return 1
            del self.m_contexts[key]

        return 0

    # 命中时 value 为现值，key 为空或未命中时为默认值；返回：0 命中 / 1 key 为空 / 2 key 不存在
    def get(self, key, default_value):
        if not key:
            log.error("key is empty, please check Context.m_key.")
            return 1, default_value

        with self.m_contextsMutex.gen_rlock():
            if key not in self.m_contexts:
                log.error(
                    f"key is not exist: {key}, please check Context.m_key.")
                return 2, default_value
            return 0, self.m_contexts[key].value

    # 返回：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
    def set(self, key, value):
        if not key:
            log.error("key is empty, please check Context.m_key.")
            return 1

        # 本次提交的键值快照：checker 校验与 monitor 通知统一回传该临时对象
        data = Context(key, value)

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                log.error(
                    f"key is not exist: {key}, please check Context.m_key.")
                return 2

            # 类型校验同名 key 的值类型名必须一致（新值为 None 时
            # NoneType 必不匹配，自然被拒）
            if type(entry.value).__name__ != type(value).__name__:
                log.error(
                    f"context type not match: {key}, please check Context.m_value.")
                return 3

            if self.m_checkerEnabled and entry.checker:
                if (entry.checker(data) == CheckStatus.eReject):
                    return 4

            entry.value = value
            monitors_snapshot = list(entry.monitors)

            # 异步入队在写锁内完成：入队序 = 提交序；配合单线程 FIFO 池 ⇒
            # 异步通知恒按 set 提交序送达，并发 set 亦然。
            # 锁序 m_contextsMutex -> 池内部锁单向安全：worker 锁外执行、stop 不持本锁调用
            monitor_enabled = self.m_monitorEnabled
            if monitor_enabled:
                for monitor in monitors_snapshot:
                    if (monitor.async_monitor_func):
                        self._post_monitor(monitor.async_monitor_func, data)

        if monitor_enabled:
            for monitor in monitors_snapshot:
                if not monitor.async_monitor_func:
                    # 同步 monitor 异常防护：用户回调异常记日志吞掉，不击穿 set 调用链，
                    # 单个回调异常不影响后续 monitor 执行
                    try:
                        monitor.monitor_func(data)
                    except Exception as e:
                        log.error(
                            f"sync monitor threw exception: {e}, ignored.")

        return 0

    def _post_monitor(self, func, ctx):
        # 异步监控投递：池未创建或已停止时拒绝并记日志丢弃
        if self.m_monitorPool is None:
            log.error("context monitor pool is stopped, async monitor ignored.")
            return
        try:
            self.m_monitorPool.submit(self._run_monitor_safe, func, ctx)
        except RuntimeError:
            # 池 shutdown 后 submit 拒新
            log.error("context monitor pool is stopped, async monitor ignored.")

    @staticmethod
    def _run_monitor_safe(func, ctx):
        # 池 worker 兜底：异步监控回调异常记日志吞掉，不终止 worker
        try:
            func(ctx)
        except Exception as e:
            log.error(f"async monitor threw exception: {e}, ignored.")

    def turn_on_checker(self):
        self.m_checkerEnabled = True

    def turn_off_checker(self):
        self.m_checkerEnabled = False

    def turn_on_monitor(self):
        self.m_monitorEnabled = True

    def turn_off_monitor(self):
        self.m_monitorEnabled = False

    # 返回：0 成功 / 1 key 为空 / 2 check_func 为空 / 3 key 不存在
    def set_checker(self, key, check_func):
        if not key:
            log.error("key is empty, please check ContextChecker.m_key.")
            return 1
        if not check_func:
            log.error(
                "check_func is empty, please check ContextChecker.m_check_func.")
            return 2

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                log.info(
                    f"YomkContext key: {key} is not exist, please check ContextChecker.m_key.")
                return 3
            entry.checker = check_func

        return 0

    # 返回：0 成功 / 1 key 为空 / 2 monitor_func 为空 / 3 key 不存在
    def set_monitor(self, key, monitor_func, async_monitor):
        if not key:
            log.error("key is empty, please check ContextMonitor.m_key.")
            return 1
        if not monitor_func:
            log.error(
                "monitor_func is empty, please check ContextMonitor.m_monitor_func.")
            return 2

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(key)
            if entry is None:
                log.info(
                    f"YomkContext key: {key} is not exist, please check ContextMonitor.m_key.")
                return 3
            entry.monitors.append(Monitor(monitor_func, async_monitor))

        return 0

    def keys(self):
        with self.m_contextsMutex.gen_rlock():
            # 排序输出:map 迭代序（字典序）
            return sorted(self.m_contexts.keys())

    # 命中时返回 (0, ContextKeyInfo)；返回：0 命中 / 1 key 为空 / 2 key 不存在
    def key_info(self, key):
        if not key:
            log.error("key is empty, please check key.")
            return 1, None

        # 命中：锁内构造结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        with self.m_contextsMutex.gen_rlock():
            if key not in self.m_contexts:
                log.error(
                    f"YomkContext key: {key} is not exist, please check key.")
                return 2, None
            entry = self.m_contexts[key]
            async_count = 0
            for monitor in entry.monitors:
                if monitor.async_monitor_func:
                    async_count += 1
            info = ContextKeyInfo(
                key,
                type(entry.value).__name__ if entry.value is not None else "",
                bool(entry.checker),
                len(entry.monitors),
                async_count,
                self.m_name)
            return 0, info

    def list_all(self):
        # 锁内构造结构化快照列表，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）；
        # 快照携带所属组名
        infos = []
        with self.m_contextsMutex.gen_rlock():
            # 排序遍历:map 迭代序（字典序）
            for key in sorted(self.m_contexts.keys()):
                entry = self.m_contexts[key]
                async_count = 0
                for monitor in entry.monitors:
                    if monitor.async_monitor_func:
                        async_count += 1
                infos.append(ContextKeyInfo(
                    key,
                    type(entry.value).__name__ if entry.value is not None else "",
                    bool(entry.checker),
                    len(entry.monitors),
                    async_count,
                    self.m_name))
        return infos


class YomkContext(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_main = ContextGroup("main")
        # 自定义组表：按组名惰性自动创建、不支持销毁（只增不减）；
        # 组名唯一由 dict key 天然保证，同名寻址即复用同组
        self.m_customGroup = {}
        self.m_customGroupMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/create", self.create, "Context")
        self.install_func("/destroy", self.destroy, "ContextGroupKey")
        self.install_func("/get", self.get, "Context")
        self.install_func("/set", self.set, "Context")
        self.install_func("/turn_on_checker", self.turn_on_checker)
        self.install_func("/turn_off_checker", self.turn_off_checker)
        self.install_func("/set_checker", self.set_checker, "ContextChecker")
        self.install_func("/turn_on_monitor", self.turn_on_monitor)
        self.install_func("/turn_off_monitor", self.turn_off_monitor)
        self.install_func("/set_monitor", self.set_monitor, "ContextMonitor")
        self.install_func("/groups", self.groups)
        self.install_func("/keys", self.keys)
        self.install_func("/key", self.key_info, "ContextGroupKey")
        self.install_func("/all", self.list_all)

    def deinit(self):
        # 排空式停止监控池由组自持（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
        # stop 幂等，重复 deinit 安全
        self.m_main.stop()
        # 自定义组：锁内拷出并清表、锁外逐组排空停止（保持"deinit 不持锁调 stop"）
        with self.m_customGroupMutex.gen_wlock():
            groups = list(self.m_customGroup.values())
            self.m_customGroup.clear()
        for group in groups:
            group.stop()

    def find_group(self, group_name):
        # 组路由辅助：main 组成员直取；自定义组按名惰性自动创建（不支持销毁，只增不减）；
        # 返回 (group, fail)：fail 非 None 时调用方直接返回该错误响应
        if not group_name:
            log.error("group name is empty, please check Context.group.")
            return None, YomkResponse(ResStatus.eNo, "group name is empty")
        if group_name == "main":
            return self.m_main, None

        # 非 main：读锁快查自定义组表
        with self.m_customGroupMutex.gen_rlock():
            group = self.m_customGroup.get(group_name)
        if group is not None:
            return group, None
        # 未命中：写锁内 double-check 后惰性创建（构造 + 入表锁内原子完成，避免并发双建）；
        # 表持有强引用且只增不减，锁外使用长期安全
        with self.m_customGroupMutex.gen_wlock():
            group = self.m_customGroup.get(group_name)
            if group is None:
                group = ContextGroup(group_name)
                self.m_customGroup[group_name] = group
        return group, None

    def turn_on_monitor(self, pkg):
        # 组名走 API 请求包（裸字符串）；历史调用传 None 时按缺省组 main 兜底
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_on_monitor()
        return YomkResponse(ResStatus.eOk, "turn on monitor success")

    def turn_off_monitor(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_off_monitor()
        return YomkResponse(ResStatus.eOk, "turn off monitor success")

    def set_monitor(self, contextMonitor):
        if not isinstance(contextMonitor, ContextMonitor):
            raise TypeError("contextMonitor must be ContextMonitor")

        group, fail = self.find_group(contextMonitor.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_monitor）：0 成功 / 1 key 为空 / 2 monitor_func 为空 / 3 key 不存在
        rc = group.set_monitor(
            contextMonitor.key,
            contextMonitor.monitor_func,
            contextMonitor.async_monitor_func)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "context monitor function is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "set context monitor success")

    def set_checker(self, contextChecker):
        if not isinstance(contextChecker, ContextChecker):
            raise TypeError("contextChecker must be ContextChecker")

        group, fail = self.find_group(contextChecker.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set_checker）：0 成功 / 1 key 为空 / 2 check_func 为空 / 3 key 不存在
        rc = group.set_checker(
            contextChecker.key, contextChecker.check_func)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "checkFunc is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "set checker success")

    def turn_on_checker(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_on_checker()
        return YomkResponse(ResStatus.eOk, "turn on checker success")

    def turn_off_checker(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        group.turn_off_checker()
        return YomkResponse(ResStatus.eOk, "turn off checker success")

    def create(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.create）：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
        rc = group.create(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "value is empty")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "key already exists")
        return YomkResponse(ResStatus.eOk, "create context success")

    def destroy(self, groupKey):
        if not isinstance(groupKey, ContextGroupKey):
            raise TypeError("groupKey must be ContextGroupKey")

        group, fail = self.find_group(groupKey.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.destroy）：0 成功 / 1 key 不存在
        rc = group.destroy(groupKey.key)
        if rc != 0:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "destroy context success")

    def get(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.get）：0 命中 / 1 key 为空 / 2 key 不存在；
        # 未命中 value 已为默认值，msg 注明返回默认值（提示 data 非存储值）
        rc, value = group.get(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty, return default value.", value)
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist, return default value.", value)
        return YomkResponse(ResStatus.eOk, "get context success", value)

    def set(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        group, fail = self.find_group(ctx.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.set）：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
        rc = group.set(ctx.key, ctx.value)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        if rc == 3:
            return YomkResponse(ResStatus.eNo, "context type not match")
        if rc == 4:
            return YomkResponse(ResStatus.eNo, "checker reject set context")
        return YomkResponse(ResStatus.eOk, "set context success")

    def groups(self, pkg):
        # 全部组名：main 恒在首位，自定义组按字典序追加（锁内拷出、锁外拼响应）
        groupNames = [self.m_main.name()]
        with self.m_customGroupMutex.gen_rlock():
            groupNames.extend(sorted(self.m_customGroup.keys()))
        return YomkResponse(ResStatus.eOk, "ok", groupNames)

    def keys(self, pkg):
        group, fail = self.find_group(pkg if isinstance(pkg, str) else "main")
        if group is None:
            return fail
        return YomkResponse(ResStatus.eOk, "ok", group.keys())

    def key_info(self, pkg):
        if not isinstance(pkg, ContextGroupKey):
            raise TypeError("pkg must be ContextGroupKey")

        group, fail = self.find_group(pkg.group)
        if group is None:
            return fail

        # 返回码映射（约定见 ContextGroup.key_info）：0 命中 / 1 key 为空 / 2 key 不存在
        rc, info = group.key_info(pkg.key)
        if rc == 1:
            return YomkResponse(ResStatus.eNo, "key is empty")
        if rc == 2:
            return YomkResponse(ResStatus.eNo, "key is not exist")
        return YomkResponse(ResStatus.eOk, "ok", info)

    def list_all(self, pkg):
        # 跨组聚合：组 → 键元信息两层结构（对齐 SERVER_INFO_ALL 的服务拓扑范式）；
        # main 组条目恒在首位，自定义组按字典序追加（锁内拷出、锁外取快照）
        groups = [ContextGroupInfo(self.m_main.name(), self.m_main.list_all())]
        with self.m_customGroupMutex.gen_rlock():
            customGroups = [self.m_customGroup[name]
                            for name in sorted(self.m_customGroup.keys())]
        for group in customGroups:
            groups.append(ContextGroupInfo(group.name(), group.list_all()))
        return YomkResponse(ResStatus.eOk, "ok", groups)


class YomkFunctionPool(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkFunctionPool")
        self.m_functions = {}     # {name: Function}（func+msg_name 同存）
        self.m_functionsMutex = rwlock.RWLockFair()

    def init(self):
        # URL 装配顺序/register /unregister /call /names /name /all
        self.install_func("/register", self.registerFunction, "Function")
        self.install_func("/unregister", self.unRegisterFunction, "String")
        self.install_func("/call", self.callFunction, "CallFunction")
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/names", self.funcNames)
        self.install_func("/name", self.funcInfo, "String")
        self.install_func("/all", self.listAll)

    def registerFunction(self, function: Function):
        if not isinstance(function, Function):
            raise TypeError("function must be Function")

        # 合一校验：空名或空函数体均视为参数无效；
        # eNo 为 not-found/业务拒绝框架惯例，eInvalid 仅用于参数无效
        if not function.name or not function.func:
            log.error(
                "funcName or func is empty, please check Function.m_funcName")
            return YomkResponse(ResStatus.eInvalid, "funcName or func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                # 重复名=更新语义：整对象替换覆盖 func、msg_name、description 三字段
                # （未在替换对象上声明的元数据即被清除），先更新再返回显式区分的文案
                log.info(
                    f"function name: {function.name} is already exist, update to current function")
                self.m_functions[function.name] = function
                return YomkResponse(
                    ResStatus.eOk,
                    "find function name is already exist, update to current function")
            self.m_functions[function.name] = function

        return YomkResponse(ResStatus.eOk, "register function success")

    def unRegisterFunction(self, func_name: str):
        # 注销已注册函数：参数无效 eInvalid、
        # 未注册 eNo（not-found 框架惯例；eInvalid 仅用于参数无效）
        if not func_name:
            log.error("funcName is empty, please check String.")
            return YomkResponse(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_wlock():
            if func_name not in self.m_functions:
                log.error(
                    f"funcName: {func_name} is not register, can not unregister")
                return YomkResponse(ResStatus.eNo, "funcName is not register")
            del self.m_functions[func_name]

        return YomkResponse(ResStatus.eOk, "unregister function success")

    def callFunction(self, call_func: CallFunction):
        if not isinstance(call_func, CallFunction):
            raise TypeError("call_func must be CallFunction")

        if not call_func.name:
            log.error("funcName is empty, please check CallFunction.m_funcName.")
            return YomkResponse(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            if call_func.name not in self.m_functions:
                log.error(
                    f"funcName: {call_func.name} is not register, please check CallFunction.m_funcName.")
                return YomkResponse(ResStatus.eNo, "funcName is not register")
            else:
                func = self.m_functions[call_func.name].func

        return func(call_func.pkg)

    def funcNames(self, pkg):
        # 内省：列出全部注册函数名（读锁内排序，:map 字典序）
        with self.m_functionsMutex.gen_rlock():
            names = sorted(self.m_functions)
        return YomkResponse(ResStatus.eOk, "ok", names)

    def funcInfo(self, func_name: str):
        # 内省：单个函数元信息查询，命中返回结构化 FuncInfo 包（msg 恒 "ok"，数据走 data）
        if not func_name:
            log.error("funcName is empty, please check String.")
            return YomkResponse(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            entry = self.m_functions.get(func_name)
            if entry is None:
                log.error(
                    f"funcName: {func_name} is not register, please check funcName")
                return YomkResponse(ResStatus.eNo, "funcName is not register")
            # 命中：结构化包恒挂，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
            info = FuncInfo(func_name, entry.msg_name, entry.description)

        return YomkResponse(ResStatus.eOk, "ok", info)

    def listAll(self, pkg):
        # 内省：全量函数元信息，读锁内构造结构化快照列表
        # （msg 恒 "ok"，数据走 data；函数总数由列表长度承载）
        with self.m_functionsMutex.gen_rlock():
            infos = [FuncInfo(name, entry.msg_name, entry.description)
                     for name, entry in sorted(self.m_functions.items())]
        return YomkResponse(ResStatus.eOk, "ok", infos)


class EventLoop:
    def __init__(self):
        self.m_event_queue = queue.Queue()
        self.m_queueMutex = threading.Lock()
        self.m_condition = threading.Condition(self.m_queueMutex)
        self.m_thread = None
        self.m_running = False
        self.m_event_id = 1
        self.m_defaultServiceFunc = None
        self.m_defaultMsgName = ""

    def set_default_service_func(self, func, msg_name=""):
        # 双参msg_name 同存，infoLine 内省依赖
        self.m_defaultServiceFunc = func
        self.m_defaultMsgName = msg_name

    def run(self):
        while self.m_running:
            with self.m_queueMutex:
                self.m_condition.wait_for(
                    lambda: not self.m_event_queue.empty() or not self.m_running)

                if not self.m_running:
                    break

                try:
                    event = self.m_event_queue.get_nowait()
                except queue.Empty:
                    continue

            if not event:
                log.error("EventLoop: event is null, please check event")
                continue
            # 异常吞噬后仍无条件触发等待回调
            try:
                event.handle()
            except Exception as e:
                log.error(
                    f"EventLoop: {event.m_eventLoopName} exec event id: {event.m_eventId} caught, what: {e}")
            except BaseException:
                log.error("EventLoop: unknown exception caught")
            if event.m_waitCallback:
                event.m_waitCallback()

    def start(self):
        if self.m_running:
            log.error("EventLoop already running, please do not start it again")
            return 0
        # 持队列锁创建线程
        with self.m_queueMutex:
            self.m_running = True
            self.m_thread = threading.Thread(target=self.run)
            self.m_thread.start()
        return 0

    def stop(self):
        if not self.m_running:
            return 0
        self.m_running = False
        # 仅退线程不清队列：未执行事件保留，待 start 续跑
        with self.m_queueMutex:
            self.m_condition.notify_all()
        if self.m_thread and self.m_thread.is_alive():
            self.m_thread.join()
        return 0

    def destroy(self):
        # stop 后锁内清空队列，锁外逐个触发被丢事件的等待回调（释放 postWait 等待者）；
        # 锁外调回调避免与 postWait 持临时锁回调构成 ABBA 死锁；幂等可重复进入
        self.stop()
        discarded = []
        with self.m_queueMutex:
            while not self.m_event_queue.empty():
                discarded.append(self.m_event_queue.get_nowait())
        for event in discarded:
            if event and event.m_waitCallback:
                event.m_waitCallback()
        return 0

    def __del__(self):
        # 析构兜底：与显式 destroy 共享释放契约（丢弃事件触发等待回调）；
        # try 防护解释器关闭期属性缺失；引用环场景需 gc.collect() 显式触发（python 无 C++ 析构确定性）
        try:
            self.destroy()
        except Exception:
            pass

    def post(self, event):
        # 判序先判运行态（返 2，与入队成功 0/事件为空 1 区分，供上层映射错误码），后判事件
        if not self.m_running:
            log.error("EventLoop not running, please start event loop.")
            return 2

        if not event:
            log.error("EventLoop: event is null, please check event")
            return 1

        with self.m_queueMutex:
            self.m_event_id += 1
            event.m_eventId = self.m_event_id
            if event.m_serviceFunc is None:
                event.m_serviceFunc = self.m_defaultServiceFunc
            self.m_event_queue.put(event)
            self.m_condition.notify_all()

        return 0

    def post_wait(self, event):
        # 前置检查与 post 全同
        if not self.m_running:
            log.error("EventLoop not running, please start event loop.")
            return 2

        if not event:
            log.error("EventLoop: event is null, please check event")
            return 1

        # 自 join 防护读取持队列锁：与 start 持锁创建线程互斥串行，避免无锁并发读写
        with self.m_queueMutex:
            in_worker = self.m_thread is not None and threading.get_ident() == self.m_thread.ident
        if in_worker:
            log.error(
                "EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve "
                "deadlock")
            event.handle()
            return 0

        tmp_mutex = threading.Lock()
        tmp_condition = threading.Condition(tmp_mutex)
        notified = [False]

        def callback():
            with tmp_mutex:
                notified[0] = True
                tmp_condition.notify_all()

        with tmp_condition:
            event.m_waitCallback = callback
            # TOCTOU 防护：仅在实际入队成功后才等待——检查与入队之间循环被停止/销毁时
            # post 返回非零，直接返回避免等待一个永不被执行/触发的回调
            rc = self.post(event)
            if rc != 0:
                return rc
            while not notified[0]:
                tmp_condition.wait()

        return 0

    def info_snapshot(self, info, tag_count):
        # 结构化内省快照：填充事件循环元信息（/YomkEventLoop/loop 返回数据）；
        # 队列锁内取数；队列头起最多 tag_count 条事件 tag，存原值（空串保留，展示层 "-" 占位由 info_line 处理）
        with self.m_queueMutex:
            info.running = self.m_running
            info.pending = self.m_event_queue.qsize()
            info.has_default_func = self.m_defaultServiceFunc is not None
            info.msg_name = self.m_defaultMsgName
            for event in list(self.m_event_queue.queue)[:tag_count]:
                info.next_tags.append(event.m_tag if event else "")

    def info_line(self, loop_name, tag_count):
        # 内省元信息行：结构化快照取数，展示层拼行；
        # 格式 "名 running:on|off pending:N defaultFunc:on|off [类型名] nextNEventTag(N): tag1, tag2..."
        info = EventLoopInfo()
        self.info_snapshot(info, tag_count)

        # 队首最多列出 tag_count 个事件 tag，队列不足则全部列出；空 tag 显示 "-" 占位
        tag_list = ", ".join(tag if tag else "-" for tag in info.next_tags)
        return (loop_name + (" running:on" if info.running else " running:off") +
                " pending:" + str(info.pending) +
                (" defaultFunc:on" if info.has_default_func else " defaultFunc:off") +
                ((" [" + info.msg_name + "]") if info.has_default_func and info.msg_name else "") +
                " nextNEventTag(" + str(tag_count) + "): " + tag_list)


class YomkEventLoop(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkEventLoop")
        self.m_eventLoop = {}
        self.m_eventLoopMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/start", self.start, "Eventloop")
        self.install_func("/stop", self.stop, "String")
        self.install_func("/post", self.post, "Event")
        self.install_func("/post_wait", self.post_wait, "Event")
        self.install_func("/destroy", self.destroy, "String")
        # 调试内省接口，端点挂在本服务 funcMap
        self.install_func("/loops", self.loops)
        self.install_func("/loop", self.loop_info, "String")
        self.install_func("/all", self.list_all)
        return 0

    def start(self, pkg: EventLoopPkg):
        if not isinstance(pkg, EventLoopPkg):
            raise TypeError("pkg must be EventLoopPkg")
        # 空名是合法 map 键
        with self.m_eventLoopMutex.gen_wlock():
            if pkg.m_eventloopName in self.m_eventLoop:
                # 已存在仅重启，不重设默认函数
                self.m_eventLoop[pkg.m_eventloopName].start()
                return YomkResponse(ResStatus.eOk, "event loop start success")
            event_loop = EventLoop()
            event_loop.set_default_service_func(
                pkg.m_defaultServiceFunc, pkg.m_msgName)
            event_loop.start()
            self.m_eventLoop[pkg.m_eventloopName] = event_loop
            return YomkResponse(ResStatus.eOk, "event loop start success")

    def stop(self, pkg):
        with self.m_eventLoopMutex.gen_rlock():
            if pkg in self.m_eventLoop:
                self.m_eventLoop[pkg].stop()
                return YomkResponse(ResStatus.eOk, "event loop stop success")
            else:
                log.error("event loop: " + pkg +
                          " not exist, please check event loop name")
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def destroy(self, pkg):
        with self.m_eventLoopMutex.gen_wlock():
            if pkg in self.m_eventLoop:
                # 调底层 destroy（stop+清队列+释放等待者）后出表
                self.m_eventLoop[pkg].destroy()
                del self.m_eventLoop[pkg]
                return YomkResponse(ResStatus.eOk, "event loop destroy success")
            else:
                log.error("event loop: " + pkg +
                          " not exist, please check event loop name")
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def post(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                # 内层返回码映射：0 成功；其余（实际仅 2=循环未运行被拒）统一 eNo
                rc = self.m_eventLoop[pkg.m_eventLoopName].post(pkg)
                if rc != 0:
                    return YomkResponse(ResStatus.eNo, "event loop not running, post rejected")
                return YomkResponse(ResStatus.eOk, "event loop post success")
            else:
                log.error("event loop: " + pkg.m_eventLoopName +
                          " not exist, please check event loop name")
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def post_wait(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                # 内层返回码映射：0 成功（回传 event 携带执行结果）；其余统一 eNo（未入队未执行，不回传）
                rc = self.m_eventLoop[pkg.m_eventLoopName].post_wait(pkg)
                if rc != 0:
                    return YomkResponse(ResStatus.eNo, "event loop not running, post wait rejected")
                return YomkResponse(ResStatus.eOk, "event loop post wait success", pkg)
            else:
                log.error("event loop: " + pkg.m_eventLoopName +
                          " not exist, please check event loop name")
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def loops(self, pkg):
        # 内省：列出全部事件循环名
        with self.m_eventLoopMutex.gen_rlock():
            loop_names = list(self.m_eventLoop.keys())
        return YomkResponse(ResStatus.eOk, "ok", loop_names)

    def loop_info(self, pkg):
        # 内省：单个事件循环元信息，入参格式 "循环名" 或 "循环名 N"（N 缺省 3）
        input_str = pkg if pkg else ""
        loop_name = input_str
        tag_count = 3
        space_pos = input_str.rfind(" ")
        if space_pos != -1:
            count_str = input_str[space_pos + 1:]
            # 尾段非空纯 ASCII 数字才拆
            if count_str and all(c in "0123456789" for c in count_str):
                loop_name = input_str[:space_pos]
                # 超过无符号上限时回退默认 3，不抛不崩
                tag_count = int(count_str) if int(
                    count_str) <= 2 ** 64 - 1 else 3

        with self.m_eventLoopMutex.gen_rlock():
            if loop_name in self.m_eventLoop:
                # 锁内拷循环对象，锁外取数（info_snapshot 自锁队列锁）
                event_loop = self.m_eventLoop[loop_name]
            else:
                log.error("event loop: " + loop_name +
                          " not exist, please check event loop name")
                return YomkResponse(ResStatus.eNo, "event loop not exist")

        # 命中：结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        info = EventLoopInfo(loop_name)
        event_loop.info_snapshot(info, tag_count)
        return YomkResponse(ResStatus.eOk, "ok", info)

    def list_all(self, pkg):
        # 内省：全部事件循环元信息，锁内拷贝快照、锁外逐循环取结构化快照
        # （msg 恒 "ok"，数据走 data；next_tags 取 3 条与单查缺省一致）
        with self.m_eventLoopMutex.gen_rlock():
            pairs = list(self.m_eventLoop.items())
        infos = []
        for name, loop in pairs:
            info = EventLoopInfo(name)
            loop.info_snapshot(info, 3)
            infos.append(info)
        return YomkResponse(ResStatus.eOk, "ok", infos)

    def deinit(self):
        # 收尾残留 loop：写锁内快照并清表，锁外逐个 destroy
        # （stop+清队列+释放等待者）；锁外清理模式与 shutdown 先例一致
        with self.m_eventLoopMutex.gen_wlock():
            loops = list(self.m_eventLoop.values())
            self.m_eventLoop.clear()
        for loop in loops:
            loop.destroy()


class YomkLogger(YomkService):
    # 日志模块服务：14 端点装配序与契约消息逐字对齐。
    # 锁序约定：嵌套双读仅 list_all 内省快照，一律按 m_consoleLoggersMutex ->
    # m_fileLoggersMutex 的顺序持有，全仓无反向嵌套点（loggers 按类型单锁查表，
    # delete_logger 分段持锁不嵌套；其余端点含 set_named_console_level/
    # set_named_file_level 均单锁）；m_consoleLogProxyMutex 为叶子锁，
    # 临界区内不得获取任何其他锁（current_console_state 在表锁外先独立取 proxy 锁，
    # 沿用 proxy 锁独立作用域先释放的既有模式）。
    # 后续维护新增嵌套时须沿用此序，否则有死锁风险。两表保持 dict 但内省输出显式
    # sorted :map 字典序（python dict 为插入序，ServerInfo 轮先例）。
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        self.set_name("/YomkLogger")
        self.m_consoleLoggers = {"MainLogger": ConsoleLogger()}
        self.m_consoleLoggersMutex = rwlock.RWLockFair()
        self.m_fileLoggers = {}
        self.m_fileLoggersMutex = rwlock.RWLockFair()
        self.m_showConsoleDebugLog = True
        self.m_showConsoleInfoLog = True
        self.m_showConsoleWarningLog = True
        self.m_showConsoleErrorLog = True
        # 文件日志全局主闸（第一层）：与控制台主闸相互独立，输出取主闸×逐器两层 AND；
        # 逐器门控随 FileLogger sink 自持（第二层）
        self.m_showFileDebugLog = True
        self.m_showFileInfoLog = True
        self.m_showFileWarningLog = True
        self.m_showFileErrorLog = True
        # proxy 字段专属锁：锁内拷贝快照、锁外调用回调，避免持锁重入
        self.m_consoleLogProxyMutex = threading.Lock()
        self.m_consoleLogProxy = False
        self.m_consoleLogProxyFunc = None

    def init(self):
        self.install_func("/set_console_log_proxy",
                          self.set_console_log_proxy, "ConsoleLogProxy")
        self.install_func("/console_log", self.console_log, "Log")
        self.install_func("/create_file_logger",
                          self.create_file_logger, "LogFile")
        self.install_func("/file_log", self.file_log, "Log")
        self.install_func("/write_file_log", self.write_file_log, "String")
        self.install_func("/delete_logger", self.delete_logger, "String")
        self.install_func("/off_console_log_by_level",
                          self.off_console_log_by_level, "Log")
        self.install_func("/on_console_log_by_level",
                          self.on_console_log_by_level, "Log")
        # 调试内省接口，端点挂在本服务 funcMap；/loggers、/logger 均传类型参数
        self.install_func("/loggers", self.loggers, "String")
        self.install_func("/logger", self.logger_info, "LoggerQuery")
        self.install_func("/all", self.list_all)
        self.install_func("/console_global_state", self.console_global_state)
        self.install_func("/off_file_log_by_level",
                          self.off_file_log_by_level, "Log")
        self.install_func("/on_file_log_by_level",
                          self.on_file_log_by_level, "Log")
        self.install_func("/file_global_state", self.file_global_state)
        return 0

    @staticmethod
    def _unpack(pkg, cls):
        # eNo + " pkg is null or
        # pkg is not <Type>. "（首尾空格为消息字面量约定；String 端点包为裸 str）
        if isinstance(pkg, cls):
            return None
        type_name = "String" if cls is str else cls.__name__
        log.error("pkg is null or pkg is not " + type_name + ".")
        return YomkResponse(ResStatus.eNo, " pkg is null or pkg is not " + type_name + ". ")

    def console_log(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad
        if not pkg.m_logger:
            _yomk_err_pos_log("console logger name is empty, use MainLogger")
            pkg.m_logger = "MainLogger"

        # 锁内拷贝 proxy 快照，锁外调用回调（避免用户回调持锁重入）
        with self.m_consoleLogProxyMutex:
            proxy_enabled = self.m_consoleLogProxy
            proxy_func = self.m_consoleLogProxyFunc
        if proxy_enabled and proxy_func and not proxy_func(pkg):
            return YomkResponse(ResStatus.eOk, "console log proxy is success.")

        # 双检锁：读锁 miss 后升级写锁二次查找，logger 引用拷出锁外使用
        console_logger = None
        with self.m_consoleLoggersMutex.gen_rlock():
            found = self.m_consoleLoggers.get(pkg.m_logger)
            if found is not None:
                console_logger = found
        if console_logger is None:
            with self.m_consoleLoggersMutex.gen_wlock():
                console_logger = self.m_consoleLoggers.get(pkg.m_logger)
                if console_logger is None:
                    new_logger = ConsoleLogger()
                    new_logger.set_name(pkg.m_logger)
                    self.m_consoleLoggers[pkg.m_logger] = new_logger
                    console_logger = new_logger

        if pkg.m_level == Log.eInfo:
            if self.m_showConsoleInfoLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eWarn:
            if self.m_showConsoleWarningLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eError:
            if self.m_showConsoleErrorLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        elif pkg.m_level == Log.eDebug:
            if self.m_showConsoleDebugLog:
                console_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
        else:
            _yomk_err_pos_log("unknown log level, use Info")
            if self.m_showConsoleInfoLog:
                console_logger.log(Log.eInfo, pkg.m_tag, pkg.m_log)

        return YomkResponse(ResStatus.eOk, "success.")

    def set_console_log_proxy(self, pkg: ConsoleLogProxy):
        bad = self._unpack(pkg, ConsoleLogProxy)
        if bad:
            return bad
        # 与 consoleLog 读侧互斥；传 None 即卸载 proxy、恢复默认输出；两字段同锁一致更新
        with self.m_consoleLogProxyMutex:
            self.m_consoleLogProxyFunc = pkg.m_consoleLogProxyFunc
            self.m_consoleLogProxy = self.m_consoleLogProxyFunc is not None
        return YomkResponse(ResStatus.eOk, "success.")

    def create_file_logger(self, pkg: LogFile):
        bad = self._unpack(pkg, LogFile)
        if bad:
            return bad

        # 空名/空目录拒绝：eInvalid（参数无效惯例，P2-a 处置）
        if not pkg.m_logger:
            _yomk_err_pos_log("logger name is empty.")
            return YomkResponse(ResStatus.eInvalid, "logger name is empty.")
        if not pkg.m_dir:
            _yomk_err_pos_log("logger dir is empty.")
            return YomkResponse(ResStatus.eInvalid, "logger dir is empty.")

        # 全程持独占锁
        with self.m_fileLoggersMutex.gen_wlock():
            if pkg.m_logger in self.m_fileLoggers:
                return YomkResponse(ResStatus.eNo, "logger name already exists.")

            file_logger = FileLogger()
            file_logger.set_name(pkg.m_logger)
            file_logger.set_dir(pkg.m_dir)
            if not file_logger.init():
                _yomk_err_pos_log("init file logger failed: " + pkg.m_logger)
                return YomkResponse(ResStatus.eNo, "init file logger failed.")
            self.m_fileLoggers[pkg.m_logger] = file_logger

        return YomkResponse(ResStatus.eOk, "success.")

    def file_log(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        if not pkg.m_logger:
            _yomk_err_pos_log("file logger name is empty.")
            return YomkResponse(ResStatus.eNo, "file logger name is empty.")

        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(pkg.m_logger)
            if file_logger is None:
                _yomk_err_pos_log("file logger: " +
                                  pkg.m_logger + " not found.")
                return YomkResponse(ResStatus.eNo, "file logger not found.")

            if pkg.m_level == Log.eInfo:
                if self.m_showFileInfoLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eWarn:
                if self.m_showFileWarningLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eError:
                if self.m_showFileErrorLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            elif pkg.m_level == Log.eDebug:
                if self.m_showFileDebugLog:
                    file_logger.log(pkg.m_level, pkg.m_tag, pkg.m_log)
            else:
                _yomk_err_pos_log("unknown log level, use Info")
                if self.m_showFileInfoLog:
                    file_logger.log(Log.eInfo, pkg.m_tag, pkg.m_log)

        return YomkResponse(ResStatus.eOk, "success.")

    def write_file_log(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad
        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(pkg)
            if file_logger is None:
                _yomk_err_pos_log("logger: " + pkg + " not found.")
                return YomkResponse(ResStatus.eNo, "logger not found.")
            file_logger.write()

        return YomkResponse(ResStatus.eOk, "success.")

    def delete_logger(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad

        # 空名拒绝（同 createFileLogger 惯例）
        if not pkg:
            _yomk_err_pos_log("logger name is empty.")
            return YomkResponse(ResStatus.eInvalid, "logger name is empty.")

        # 分段持锁不嵌套：删除无需跨表原子快照，同时持两把写锁会阻塞全部日志写入
        deleted_console = 0
        with self.m_consoleLoggersMutex.gen_wlock():
            if pkg in self.m_consoleLoggers:
                del self.m_consoleLoggers[pkg]
                deleted_console = 1

        # 锁内拷出、锁外释放最后引用：FileLogger __del__ 落盘 I/O 不阻塞日志写入
        deleted_file = 0
        removed_file = None
        with self.m_fileLoggersMutex.gen_wlock():
            removed_file = self.m_fileLoggers.pop(pkg, None)
            if removed_file is not None:
                deleted_file = 1
        removed_file = None   # 显式释放触发 __del__ 自动 write（缓冲内容不丢）

        if deleted_console == 0 and deleted_file == 0:
            _yomk_err_pos_log("logger: " + pkg + " not found.")
            return YomkResponse(ResStatus.eNo, "logger not found.")

        # 不删除磁盘上的 .log 文件（清理归调用方）
        return YomkResponse(
            ResStatus.eOk,
            "deleted console:" + str(deleted_console) + " file:" + str(deleted_file))

    def off_console_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_console_level(pkg.m_logger, pkg.m_level, False)

        # 空名 = 全局主闸路径（与既有语义一致）
        if pkg.m_level == Log.eInfo:
            self.m_showConsoleInfoLog = False
        elif pkg.m_level == Log.eWarn:
            self.m_showConsoleWarningLog = False
        elif pkg.m_level == Log.eError:
            self.m_showConsoleErrorLog = False
        elif pkg.m_level == Log.eDebug:
            self.m_showConsoleDebugLog = False
        else:
            _yomk_err_pos_log("unknown log level, turn off failed.")

        return YomkResponse(ResStatus.eOk, "success.")

    def on_console_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_console_level(pkg.m_logger, pkg.m_level, True)

        # 空名 = 全局主闸路径（与既有语义一致）
        if pkg.m_level == Log.eInfo:
            self.m_showConsoleInfoLog = True
        elif pkg.m_level == Log.eWarn:
            self.m_showConsoleWarningLog = True
        elif pkg.m_level == Log.eError:
            self.m_showConsoleErrorLog = True
        elif pkg.m_level == Log.eDebug:
            self.m_showConsoleDebugLog = True
        else:
            _yomk_err_pos_log("unknown log level, turn on failed.")
        return YomkResponse(ResStatus.eOk, "success.")

    def set_named_console_level(self, logger_name, level, on):
        # 具名开关唯一写入口：表读锁内查找拷引用出锁、锁外 set_level_on
        # （单锁；未命中 eNo 不惰性创建，不触碰全局主闸）
        with self.m_consoleLoggersMutex.gen_rlock():
            console_logger = self.m_consoleLoggers.get(logger_name)
            if console_logger is None:
                _yomk_err_pos_log("console logger: " +
                                  logger_name + " not found.")
                return YomkResponse(ResStatus.eNo, "console logger not found.")

        if level == Log.eInfo:
            console_logger.set_level_on(Log.eInfo, on)
        elif level == Log.eWarn:
            console_logger.set_level_on(Log.eWarn, on)
        elif level == Log.eError:
            console_logger.set_level_on(Log.eError, on)
        elif level == Log.eDebug:
            console_logger.set_level_on(Log.eDebug, on)
        else:
            _yomk_err_pos_log("unknown log level, turn " +
                              ("on" if on else "off") + " failed.")
        return YomkResponse(ResStatus.eOk, "success.")

    def off_file_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_file_level(pkg.m_logger, pkg.m_level, False)

        # 空名 = 全局主闸路径（与控制台侧双模式语义对称）
        if pkg.m_level == Log.eInfo:
            self.m_showFileInfoLog = False
        elif pkg.m_level == Log.eWarn:
            self.m_showFileWarningLog = False
        elif pkg.m_level == Log.eError:
            self.m_showFileErrorLog = False
        elif pkg.m_level == Log.eDebug:
            self.m_showFileDebugLog = False
        else:
            _yomk_err_pos_log("unknown log level, turn off failed.")

        return YomkResponse(ResStatus.eOk, "success.")

    def on_file_log_by_level(self, pkg: Log):
        bad = self._unpack(pkg, Log)
        if bad:
            return bad

        # 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
        if pkg.m_logger:
            return self.set_named_file_level(pkg.m_logger, pkg.m_level, True)

        # 空名 = 全局主闸路径（与控制台侧双模式语义对称）
        if pkg.m_level == Log.eInfo:
            self.m_showFileInfoLog = True
        elif pkg.m_level == Log.eWarn:
            self.m_showFileWarningLog = True
        elif pkg.m_level == Log.eError:
            self.m_showFileErrorLog = True
        elif pkg.m_level == Log.eDebug:
            self.m_showFileDebugLog = True
        else:
            _yomk_err_pos_log("unknown log level, turn on failed.")
        return YomkResponse(ResStatus.eOk, "success.")

    def set_named_file_level(self, logger_name, level, on):
        # 文件侧具名开关唯一写入口：表读锁内查找拷引用出锁、锁外 set_level_on
        # （单锁；未命中 eNo 不惰性创建，不触碰全局主闸）
        with self.m_fileLoggersMutex.gen_rlock():
            file_logger = self.m_fileLoggers.get(logger_name)
            if file_logger is None:
                _yomk_err_pos_log("file logger: " +
                                  logger_name + " not found.")
                return YomkResponse(ResStatus.eNo, "file logger not found.")

        if level == Log.eInfo:
            file_logger.set_level_on(Log.eInfo, on)
        elif level == Log.eWarn:
            file_logger.set_level_on(Log.eWarn, on)
        elif level == Log.eError:
            file_logger.set_level_on(Log.eError, on)
        elif level == Log.eDebug:
            file_logger.set_level_on(Log.eDebug, on)
        else:
            _yomk_err_pos_log("unknown log level, turn " +
                              ("on" if on else "off") + " failed.")
        return YomkResponse(ResStatus.eOk, "success.")

    def loggers(self, pkg):
        bad = self._unpack(pkg, str)
        if bad:
            return bad
        if pkg == "console":
            # 单读锁遍历 console 表：纯名清单，不再嵌套双锁
            with self.m_consoleLoggersMutex.gen_rlock():
                names = sorted(self.m_consoleLoggers)
        elif pkg == "file":
            # 单读锁遍历 file 表：纯名清单，不再嵌套双锁
            with self.m_fileLoggersMutex.gen_rlock():
                names = sorted(self.m_fileLoggers)
        else:
            _yomk_err_pos_log("unknown logger type: " + pkg + ".")
            return YomkResponse(ResStatus.eNo, "unknown logger type.")
        # 单表显式 sorted :map 中序字典序（python dict 为插入序，类头注释先例）
        return YomkResponse(ResStatus.eOk, "ok", names)

    def current_console_state(self):
        # 取控制台全局主闸态（/console_global_state 返回体）：四个级别开关为独立
        # 原子读（组合本无原子快照保证），proxy 状态读侧与写侧互斥（叶子锁，
        # 独立作用域），不持任何表锁
        state = LoggerState(
            bool(self.m_showConsoleDebugLog), bool(self.m_showConsoleInfoLog),
            bool(self.m_showConsoleWarningLog), bool(self.m_showConsoleErrorLog))
        with self.m_consoleLogProxyMutex:
            state.proxy_on = self.m_consoleLogProxy
        return state

    def named_console_state(self, console_logger, proxy_on):
        # 取单条 console 条目状态：四级别为逐器门控层真值（is_level_on），
        # proxyOn 由调用点在表锁外取好传入（条目不做两层 AND 代算）
        return LoggerState(
            console_logger.is_level_on(Log.eDebug),
            console_logger.is_level_on(Log.eInfo),
            console_logger.is_level_on(Log.eWarn),
            console_logger.is_level_on(Log.eError),
            proxy_on)

    def current_file_state(self):
        # 取文件全局主闸态（/file_global_state 返回体）：四个级别开关独立读
        # （组合本无原子快照保证），与控制台主闸相互独立；不持任何表锁；
        # proxy_on 为文件代理态（文件代理闭环 3 落地前恒 false）
        return LoggerState(
            bool(self.m_showFileDebugLog), bool(self.m_showFileInfoLog),
            bool(self.m_showFileWarningLog), bool(self.m_showFileErrorLog),
            False)

    def named_file_state(self, file_logger, proxy_on):
        # 取单条 file 条目状态：四级别为逐器门控层真值（is_level_on），
        # proxyOn 由调用点传入（条目不做两层 AND 代算）
        return LoggerState(
            file_logger.is_level_on(Log.eDebug),
            file_logger.is_level_on(Log.eInfo),
            file_logger.is_level_on(Log.eWarn),
            file_logger.is_level_on(Log.eError),
            proxy_on)

    def logger_info(self, pkg):
        bad = self._unpack(pkg, LoggerQuery)
        if bad:
            return bad
        # 按类型单表精确查找：两表允许同名，不再先 console 后 file 双表 fallback（同名歧义由 type 消除）；
        # 命中：锁内构造结构化快照，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）
        if pkg.m_type == "console":
            # 锁内查找拷引用出锁外使用（与 console_log 双检锁同款模式）
            console_logger = None
            with self.m_consoleLoggersMutex.gen_rlock():
                console_logger = self.m_consoleLoggers.get(pkg.m_logger_name)
                if console_logger is None:
                    _yomk_err_pos_log("console logger: " +
                                      pkg.m_logger_name + " not found.")
                    return YomkResponse(ResStatus.eNo, "logger not found.")
            # 状态段：条目报逐器门控层真值；proxyOn 在表锁外独立取（proxy 叶子锁独立作用域）
            with self.m_consoleLogProxyMutex:
                proxy_on = self.m_consoleLogProxy
            return YomkResponse(
                ResStatus.eOk, "ok",
                LoggerInfo(pkg.m_logger_name, "console", "",
                           self.named_console_state(console_logger, proxy_on)))
        if pkg.m_type == "file":
            with self.m_fileLoggersMutex.gen_rlock():
                file_logger = self.m_fileLoggers.get(pkg.m_logger_name)
                if file_logger is None:
                    _yomk_err_pos_log("file logger: " +
                                      pkg.m_logger_name + " not found.")
                    return YomkResponse(ResStatus.eNo, "logger not found.")
                # 状态段：条目报文件逐器门控层真值；proxy_on 为文件代理态
                # （文件代理闭环 3 落地前恒 false）
                return YomkResponse(
                    ResStatus.eOk, "ok",
                    LoggerInfo(pkg.m_logger_name, "file", file_logger.get_dir(),
                               self.named_file_state(file_logger, False)))
        _yomk_err_pos_log("unknown logger type: " + pkg.m_type + ".")
        return YomkResponse(ResStatus.eNo, "unknown logger type.")

    def list_all(self, pkg):
        # LoggerState 内嵌每条 LoggerInfo：console 条目报控制台逐器门控层真值
        # （named_console_state 逐条求值），file 条目报文件逐器门控层真值
        # （named_file_state 逐条求值，proxy_on 文件代理落地前恒 false）；
        # 全局主闸态迁出条目、由 /console_global_state 与 /file_global_state 承载
        # console proxyOn 在双表锁外先独立取（proxy 叶子锁独立作用域），随后传入逐条求值
        with self.m_consoleLogProxyMutex:
            proxy_on = self.m_consoleLogProxy
        # 同 loggers：按锁序嵌套双读锁取跨段快照，逐条构造 LoggerInfo
        # （与 logger_info 命中分支同构；console 段在前、file 段在后，各段字典序）
        with self.m_consoleLoggersMutex.gen_rlock():
            with self.m_fileLoggersMutex.gen_rlock():
                loggers = [
                    LoggerInfo(
                        name, "console", "",
                        self.named_console_state(
                            self.m_consoleLoggers[name], proxy_on))
                    for name in sorted(self.m_consoleLoggers)]
                loggers += [
                    LoggerInfo(
                        name, "file", self.m_fileLoggers[name].get_dir(),
                        self.named_file_state(self.m_fileLoggers[name], False))
                    for name in sorted(self.m_fileLoggers)]
        # msg 恒 "ok"，数据走 data（清单本身即 data）
        return YomkResponse(ResStatus.eOk, "ok", loggers)

    def console_global_state(self, pkg):
        # 控制台全局主闸态内省（无参形态同 /all；msg 恒 "ok"，LoggerState 包走 data）
        return YomkResponse(ResStatus.eOk, "ok", self.current_console_state())

    def file_global_state(self, pkg):
        # 文件全局主闸态内省（无参形态同 /all；msg 恒 "ok"，LoggerState 包走 data）
        return YomkResponse(ResStatus.eOk, "ok", self.current_file_state())


class YomkServerInfo(YomkService):
    # 服务表内省服务：四端点 /services /functions /function /all；
    # 经 weakref 判活（server 销毁后四端点均返 eNo）
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        # 清除基类对 server 的强引用，仅保留弱引用：
        # 否则本实例存活会连带 server 永不销毁，weakref 判活契约失效
        self.server = None
        self.m_weakServer = weakref.ref(server) if server else None
        self.set_name("/YomkServerInfo")

    def init(self):
        self.install_func("/services", self.list_services)
        self.install_func("/functions", self.list_functions, "String")
        self.install_func("/function", self.function_info, "String")
        self.install_func("/all", self.list_all)
        return 0

    def _locked_server(self):
        # server 已销毁时返回 None，由各端点统一走 destroyed 契约
        return self.m_weakServer() if self.m_weakServer else None

    def list_services(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list services ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")
        # :map）：服务名字典序输出
        return YomkResponse(ResStatus.eOk, "ok", sorted(server.service_names()))

    def list_functions(self, pkg):
        if pkg is None:
            log.error("pkg is null, please check String.d")
            return YomkResponse(ResStatus.eNo, "pkg is null")
        srv_name = pkg
        if not srv_name:
            log.error("srvName is empty, please check String.d")
            return YomkResponse(ResStatus.eInvalid, "srvName is empty")

        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list functions ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        infos = server.service_func_infos(srv_name)
        # 结构化清单：FuncInfo 三字段与 /function 单查、/all 聚合同族
        # （func_name 为带 / 前缀 url 段）；显式 sorted :map 字典序（python dict
        # 为插入序，类头注释先例）；不存在的服务返回空表（eOk）
        funcs = [infos[name] for name in sorted(infos.keys())]
        return YomkResponse(ResStatus.eOk, "ok", funcs)

    def function_info(self, pkg):
        if pkg is None:
            log.error("pkg is null, please check String.d")
            return YomkResponse(ResStatus.eNo, "pkg is null")
        url = pkg
        if not url or url[0] != "/":
            log.error("url parse error: " + url + ", please start with /")
            return YomkResponse(ResStatus.eNo,
                                "url parse error: " + url + ", please start with /")

        pos_end = url.find("/", 1)
        if pos_end == -1:
            log.error("url parse error: " + url + ", not found function name.")
            return YomkResponse(ResStatus.eNo,
                                "url parse error: " + url + ", not found function name.")

        srv_name = url[:pos_end]
        func_name = url[pos_end:]

        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, function info ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        # 命中：结构化包恒挂，明确字段承载全部返回数据（msg 恒 "ok"，数据走 data）；
        # 复用 FuncInfo 与函数池 /name 单查端点同族
        for name in sorted(server.service_names()):
            if name == srv_name:
                info = server.service_func_infos(srv_name).get(func_name)
                if info is None:
                    log.error("function not found -> " + url)
                    return YomkResponse(ResStatus.eNo, "function not found: " + url)
                return YomkResponse(ResStatus.eOk, "ok", info)

        log.error("service not found -> " + srv_name)
        return YomkResponse(ResStatus.eNo, "service not found: " + srv_name)

    def list_all(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list all ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        # 同现实现取数序：service_names() 一次，再逐服务 service_func_infos()；
        # FuncInfo 三字段与 /function 单查同族整体复用（func_name 为带 / 前缀 url 段）
        infos = []
        for srv_name in sorted(server.service_names()):
            func_infos = server.service_func_infos(srv_name)
            infos.append(ServerServiceInfo(
                srv_name, [func_infos[name] for name in sorted(func_infos.keys())]))
        # msg 恒 "ok"，数据走 data（结构化层级列表：服务 → 函数元信息）
        return YomkResponse(ResStatus.eOk, "ok", infos)
