from readerwriterlock import rwlock
from enum import Enum
import logging
import os
import queue
import threading
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
        self.status = status
        self.msg = msg
        self.data = data


class YomkService:
    def __init__(self, server: "YomkServer"):
        self.name: str = ""
        self.server: "YomkServer" = server
        self.functions: dict[str, Callable[[Any], YomkResponse]] = {}
        self.function_msgs: dict[str, str] = {}
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

    def install_func(self, name, function, msg_name=""):
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
            if msg_name:
                self.function_msgs[name] = msg_name
            else:
                # 两参覆盖安装时清除残留的旧类型元数据，保持内省与注册一致
                self.function_msgs.pop(name, None)

    def func_infos(self):
        infos: dict[str, YomkFuncInfo] = {}
        with self.rwlock_functions.gen_rlock():
            for func_name in self.functions:
                info = YomkFuncInfo(func_name)
                msg_name = self.function_msgs.get(func_name)
                if msg_name:
                    info.msg_name = msg_name
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
            # 对齐 main：hw 为 0(或 None) 兜底 2，否则取一半向上取整
            async_thread_count = 2 if not hw else (hw + 1) // 2
        self.executor = ThreadPoolExecutor(async_thread_count)

    @classmethod
    def create(cls, async_thread_count=0):
        # 创建 YomkServer 实例，async_thread_count 为异步请求线程池大小（0 表示自动检测）
        return cls(async_thread_count)

    def start_service(self, srv_names):
        # 内置服务映射表（键名带 / 前缀，对齐 main serviceCreators）
        service_creators = {
            "/YomkContext": YomkContext,
            "/YomkFunctionPool": YomkFunctionPool,
            "/YomkEventLoop": YomkEventLoop,
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
        # 创建并注册指定类型的服务（对齐 main newService<T>：构造 -> 可选命名 -> addService）
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
        # init 未显式返回（None）等价成功（对齐 main 返回 0）
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

        # 先停异步请求池（拒新 -> 排空），对齐 main 请求池停止顺序
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
                # 执行时才查表（对齐 main：投递与执行间服务变更以执行时为准）；
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
    def __init__(self, key="", value=None):
        self.key = key
        self.value = value


class CheckStatus(Enum):
    eAccept = 0
    eReject = 1


class ContextChecker:
    def __init__(self, key="", check_func=None):
        self.key = key
        self.check_func = check_func


class ContextMonitor:
    def __init__(self, key="", monitor_func=None, async_monitor_func=False):
        self.key = key
        self.monitor_func = monitor_func
        self.async_monitor_func = async_monitor_func


class Function:
    def __init__(self, name, func, msg_name=""):
        self.name = name
        self.func = func
        self.msg_name = msg_name


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


class Monitor:
    def __init__(self, monitor_func=None, async_monitor_func=False):
        self.monitor_func = monitor_func
        self.async_monitor_func = async_monitor_func


class YomkFuncInfo:
    def __init__(self, func_name="", msg_name=""):
        self.func_name = func_name
        self.msg_name = msg_name

# modules


class ContextEntry:
    # YomkContext 单条目内部结构（对齐 main Context{value, checker, monitors}），
    # 与消息包 Context 区分，仅服务内部使用
    def __init__(self, value):
        self.value = value
        self.checker = None    # check_func 或 None（main 条目 checker 可空）
        self.monitors = []     # [Monitor]


class YomkContext(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_contexts = {}     # {key: ContextEntry}（单条目结构对齐 main）
        self.m_contextsMutex = rwlock.RWLockFair()
        self.m_checkerEnabled = False
        self.m_monitorEnabled = False
        self.m_monitorPool = None  # 单线程 FIFO 监控池，init() 重建（对齐 main m_monitorPool）

    def init(self):
        self.install_func("/create", self.create, "Context")
        self.install_func("/destroy", self.destroy, "String")
        self.install_func("/get", self.get, "Context")
        self.install_func("/set", self.set, "Context")
        self.install_func("/turn_on_checker", self.turn_on_checker)
        self.install_func("/turn_off_checker", self.turn_off_checker)
        self.install_func("/set_checker", self.set_checker, "ContextChecker")
        self.install_func("/turn_on_monitor", self.turn_on_monitor)
        self.install_func("/turn_off_monitor", self.turn_off_monitor)
        self.install_func("/set_monitor", self.set_monitor, "ContextMonitor")
        self.install_func("/keys", self.keys)
        self.install_func("/key", self.key_info, "String")
        self.install_func("/all", self.list_all)
        # 重建监控池：池 stop 后不可复用，重建支持服务删除后重新注册；固定单线程保事件顺序（对齐 main init）
        self.m_monitorPool = ThreadPoolExecutor(1)

    def deinit(self):
        # 排空式停止监控池（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
        # shutdown 幂等，重复 deinit 安全；池对象不可复用，置 None 待 init 重建
        # （对齐 main YomkContext::deinit，由框架 del_service/同名替换/shutdown 锁外调用）
        if self.m_monitorPool is not None:
            self.m_monitorPool.shutdown(wait=True)
            self.m_monitorPool = None

    def turn_on_monitor(self, pkg):
        self.m_monitorEnabled = True
        return YomkResponse(ResStatus.eOk, "turn on monitor success")

    def turn_off_monitor(self, pkg):
        self.m_monitorEnabled = False
        return YomkResponse(ResStatus.eOk, "turn off monitor success")

    def set_monitor(self, contextMonitor):
        if not isinstance(contextMonitor, ContextMonitor):
            raise TypeError("contextMonitor must be ContextMonitor")

        if not contextMonitor.key:
            log.error("key is empty, please check ContextMonitor.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        if not contextMonitor.monitor_func:
            log.error(
                "monitor_func is empty, please check ContextMonitor.m_monitor_func.")
            return YomkResponse(ResStatus.eNo, "context monitor function is empty")

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(contextMonitor.key)
            if entry is None:
                log.info(
                    f"YomkContext key: {contextMonitor.key} is not exist, please check ContextMonitor.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")
            entry.monitors.append(Monitor(
                contextMonitor.monitor_func, contextMonitor.async_monitor_func))

        return YomkResponse(ResStatus.eOk, "set context monitor success")

    def set_checker(self, contextChecker):
        if not isinstance(contextChecker, ContextChecker):
            raise TypeError("contextChecker must be ContextChecker")

        if not contextChecker.key:
            log.error("key is empty, please check ContextChecker.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        if not contextChecker.check_func:
            log.error(
                "check_func is empty, please check ContextChecker.m_check_func.")
            return YomkResponse(ResStatus.eNo, "checkFunc is empty")

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(contextChecker.key)
            if entry is None:
                log.info(
                    f"YomkContext key: {contextChecker.key} is not exist, please check ContextChecker.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")
            entry.checker = contextChecker.check_func

        return YomkResponse(ResStatus.eOk, "set checker success")

    def turn_on_checker(self, pkg):
        self.m_checkerEnabled = True
        return YomkResponse(ResStatus.eOk, "turn on checker success")

    def turn_off_checker(self, pkg):
        self.m_checkerEnabled = False
        return YomkResponse(ResStatus.eOk, "turn off checker success")

    def create(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        if ctx.value is None:
            log.error("value is empty, please check Context.m_value.")
            return YomkResponse(ResStatus.eNo, "value is empty")

        with self.m_contextsMutex.gen_wlock():
            if ctx.key in self.m_contexts:
                log.error(
                    f"key already exists: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eNo, "key already exists")
            self.m_contexts[ctx.key] = ContextEntry(ctx.value)
        return YomkResponse(ResStatus.eOk, "create context success")

    def destroy(self, key):
        with self.m_contextsMutex.gen_wlock():
            if key not in self.m_contexts:
                log.error(f"key is not exist: {key}, please check key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")
            del self.m_contexts[key]
        return YomkResponse(ResStatus.eOk, "destroy context success")

    def get(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty", ctx.value)

        with self.m_contextsMutex.gen_rlock():
            if ctx.key not in self.m_contexts:
                log.error(
                    f"key is not exist: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist", ctx.value)
            value = self.m_contexts[ctx.key].value

        return YomkResponse(ResStatus.eOk, "get context success", value)

    def set(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        with self.m_contextsMutex.gen_wlock():
            entry = self.m_contexts.get(ctx.key)
            if entry is None:
                log.error(
                    f"key is not exist: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")

            # 类型校验对齐 main：同名 key 的值类型名必须一致（新值为 None 时
            # NoneType 必不匹配，自然被拒；main set 亦无独立空值分支）
            if type(entry.value).__name__ != type(ctx.value).__name__:
                log.error(
                    f"context type not match: {ctx.key}, please check Context.m_value.")
                return YomkResponse(ResStatus.eNo, "context type not match")

            if self.m_checkerEnabled and entry.checker:
                if (entry.checker(ctx) == CheckStatus.eReject):
                    return YomkResponse(ResStatus.eNo, "checker reject set context")

            entry.value = ctx.value
            monitors_snapshot = list(entry.monitors)

            # 异步入队在写锁内完成：入队序 = 提交序；配合单线程 FIFO 池 ⇒
            # 异步通知恒按 set 提交序送达，并发 set 亦然（对齐 main）。
            # 锁序 m_contextsMutex -> 池内部锁单向安全：worker 锁外执行、deinit 由框架锁外调用
            monitor_enabled = self.m_monitorEnabled
            if monitor_enabled:
                for monitor in monitors_snapshot:
                    if (monitor.async_monitor_func):
                        self._post_monitor(monitor.async_monitor_func, ctx)

        if monitor_enabled:
            for monitor in monitors_snapshot:
                if not monitor.async_monitor_func:
                    # 同步 monitor 异常防护：用户回调异常记日志吞掉，不击穿 set 调用链，
                    # 单个回调异常不影响后续 monitor 执行（对齐 main）
                    try:
                        monitor.monitor_func(ctx)
                    except Exception as e:
                        log.error(
                            f"sync monitor threw exception: {e}, ignored.")

        return YomkResponse(ResStatus.eOk, "set context success")

    def _post_monitor(self, func, ctx):
        # 异步监控投递：池未创建或已停止时拒绝并记日志丢弃（对齐 main post 失败路径）
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
        # 池 worker 兜底：异步监控回调异常记日志吞掉，不终止 worker（对齐 main 线程池兜底）
        try:
            func(ctx)
        except Exception as e:
            log.error(f"async monitor threw exception: {e}, ignored.")

    def keys(self, pkg):
        with self.m_contextsMutex.gen_rlock():
            # 排序输出对齐 main std::map 迭代序（字典序）
            key_list = sorted(self.m_contexts.keys())
        return YomkResponse(ResStatus.eOk, "ok", key_list)

    def key_info(self, pkg):
        key = pkg
        if not key:
            log.error("key is empty, please check key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        with self.m_contextsMutex.gen_rlock():
            if key not in self.m_contexts:
                log.error(
                    f"YomkContext key: {key} is not exist, please check key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")
            line = self._info_line(key, self.m_contexts[key])

        return YomkResponse(ResStatus.eOk, line)

    def list_all(self, pkg):
        lines = []
        with self.m_contextsMutex.gen_rlock():
            # 排序遍历对齐 main std::map 迭代序（字典序）
            for key in sorted(self.m_contexts.keys()):
                lines.append(self._info_line(key, self.m_contexts[key]))
        return YomkResponse(ResStatus.eOk, "ok", lines)

    def _info_line(self, key, entry):
        # 组装单个 key 的元信息行：key [类型名] checker:on|off monitors:N(async:M)（对齐 main contextInfoLine）
        async_count = 0
        for monitor in entry.monitors:
            if monitor.async_monitor_func:
                async_count += 1
        return (f"{key} [{type(entry.value).__name__}] "
                f"checker:{'on' if entry.checker else 'off'} "
                f"monitors:{len(entry.monitors)}(async:{async_count})")


class YomkFunctionPool(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkFunctionPool")
        self.m_functions = {}     # {name: Function}（func+msg_name 同存，对齐 main PoolFuncInfo 条目）
        self.m_functionsMutex = rwlock.RWLockFair()

    def init(self):
        # URL 装配顺序对齐 main：/register /unregister /call /names /name /all
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

        # 合一校验（对齐 main registerFunction 入口）：空名或空函数体均视为参数无效；
        # eNo 为 not-found/业务拒绝框架惯例，eInvalid 仅用于参数无效（main FPC2 统一契约）
        if not function.name or not function.func:
            log.error(
                "funcName or func is empty, please check Function.m_funcName")
            return YomkResponse(ResStatus.eInvalid, "funcName or func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                # 重复名=更新语义：整对象替换覆盖 func+msg_name 双字段（对齐 main L33-42），
                # 先更新再返回显式区分的文案
                log.info(
                    f"function name: {function.name} is already exist, update to current function")
                self.m_functions[function.name] = function
                return YomkResponse(
                    ResStatus.eOk,
                    "find function name is already exist, update to current function")
            self.m_functions[function.name] = function

        return YomkResponse(ResStatus.eOk, "register function success")

    def unRegisterFunction(self, func_name: str):
        # 注销已注册函数（对齐 main unRegisterFunction）：参数无效 eInvalid、
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
        # 内省：列出全部注册函数名（读锁内排序，对齐 main funcNames 的 std::map 字典序）
        with self.m_functionsMutex.gen_rlock():
            names = sorted(self.m_functions)
        return YomkResponse(ResStatus.eOk, "ok", names)

    def funcInfo(self, func_name: str):
        # 内省：单个函数存在性查询，命中 msg 为 "funcName [类型名]"（未声明类型时纯名）
        # （对齐 main funcInfo：参数无效 eInvalid、未注册 eNo）
        if not func_name:
            log.error("funcName is empty, please check String.")
            return YomkResponse(ResStatus.eInvalid, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            entry = self.m_functions.get(func_name)
            if entry is None:
                log.error(
                    f"funcName: {func_name} is not register, please check funcName")
                return YomkResponse(ResStatus.eNo, "funcName is not register")
            msg_name = entry.msg_name

        return YomkResponse(
            ResStatus.eOk, func_name + (f" [{msg_name}]" if msg_name else ""))

    def listAll(self, pkg):
        # 内省：全量 dump，首行 functions:N，其余每行 "funcName [类型名]"
        # （未声明类型时无括号后缀；读锁内组行保快照原子性，对齐 main listAll）
        with self.m_functionsMutex.gen_rlock():
            lines = [f"functions:{len(self.m_functions)}"]
            for name in sorted(self.m_functions):
                msg_name = self.m_functions[name].msg_name
                lines.append(name + (f" [{msg_name}]" if msg_name else ""))
        return YomkResponse(ResStatus.eOk, "ok", lines)


class EventLoop:
    def __init__(self):
        self.m_event_queue = queue.Queue()
        self.m_queueMutex = threading.Lock()
        self.m_condition = threading.Condition(self.m_queueMutex)
        self.m_thread = None
        self.m_running = False
        self.m_event_id = 1
        self.m_defaultServiceFunc = None

    def set_default_service_func(self, func):
        self.m_defaultServiceFunc = func

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
            # 异常吞噬后仍无条件触发等待回调（对齐 main：postWait 同步等待"含异常场景"完成）
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
        # 持队列锁创建线程（对齐 main 持锁 move-assign 语义：与 postWait 自 join 防护读互斥）
        with self.m_queueMutex:
            self.m_running = True
            self.m_thread = threading.Thread(target=self.run)
            self.m_thread.start()
        return 0

    def stop(self):
        if not self.m_running:
            return 0
        self.m_running = False
        # 仅退线程不清队列（对齐 main ELC2 契约）：未执行事件保留，待 start 续跑
        with self.m_queueMutex:
            self.m_condition.notify_all()
        if self.m_thread and self.m_thread.is_alive():
            self.m_thread.join()
        return 0

    def destroy(self):
        # 对齐 main：stop 后锁内清空队列，锁外逐个触发被丢事件的等待回调（释放 postWait 等待者）；
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

    def post(self, event):
        if not event:
            log.error("EventLoop: post event is null")
            return 1

        if not self.m_running:
            log.warning("EventLoop: event loop is not running")
            return 1

        with self.m_queueMutex:
            event.m_eventId = self.m_event_id
            self.m_event_id += 1
            if event.m_serviceFunc is None:
                event.m_serviceFunc = self.m_defaultServiceFunc
            self.m_event_queue.put(event)
            self.m_condition.notify_all()

        return 0

    def post_wait(self, event):
        if not event:
            log.warning("EventLoop: post event is null")
            return 1

        if not self.m_running:
            log.warning("EventLoop: event loop is not running")
            return 1

        if self.m_thread and threading.get_ident() == self.m_thread.ident:
            log.warning(
                "EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve deadlock")
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
            self.post(event)
            while not notified[0]:
                tmp_condition.wait()

        return 0


class YomkEventLoop(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkEventLoop")
        self.m_eventLoop = {str: EventLoop()}
        self.m_eventLoopMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/start", self.start, "Eventloop")
        self.install_func("/stop", self.stop, "String")
        self.install_func("/destroy", self.destroy, "String")
        self.install_func("/post", self.post, "Event")
        self.install_func("/post_wait", self.post_wait, "Event")

    def start(self, pkg: EventLoopPkg):
        if not isinstance(pkg, EventLoopPkg):
            raise TypeError("pkg must be EventLoopPkg")
        if not pkg.m_eventloopName:
            log.error(
                "event loop name is empty, please check EventLoopPkg.m_eventloopName.")
            return YomkResponse(ResStatus.eNo, "event loop name is empty")
        with self.m_eventLoopMutex.gen_wlock():
            if pkg.m_eventloopName in self.m_eventLoop:
                self.m_eventLoop[pkg.m_eventloopName].start()
                return YomkResponse(ResStatus.eOk, "event loop start success")
            else:
                event_loop = EventLoop()
                event_loop.set_default_service_func(pkg.m_defaultServiceFunc)
                self.m_eventLoop[pkg.m_eventloopName] = event_loop
                event_loop.start()
                return YomkResponse(ResStatus.eOk, "event loop start success")

    def stop(self, pkg):
        if not pkg:
            log.error(
                "event loop name is empty, please check EventLoopPkg.m_eventloopName.")
            return YomkResponse(ResStatus.eNo, "event loop name is empty")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg in self.m_eventLoop:
                self.m_eventLoop[pkg].stop()
                return YomkResponse(ResStatus.eOk, "event loop stop success")
            else:
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def destroy(self, pkg):
        if not pkg:
            log.error(
                "event loop name is empty, please check EventLoopPkg.m_eventloopName.")
            return YomkResponse(ResStatus.eNo, "event loop name is empty")
        with self.m_eventLoopMutex.gen_wlock():
            if pkg in self.m_eventLoop:
                self.m_eventLoop[pkg].stop()
                del self.m_eventLoop[pkg]
                return YomkResponse(ResStatus.eOk, "event loop destroy success")
            else:
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def post(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        if not pkg.m_eventLoopName:
            log.error(
                "event loop name is empty, please check Event.m_eventLoopName.")
            return YomkResponse(ResStatus.eNo, "event loop name is empty")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                self.m_eventLoop[pkg.m_eventLoopName].post(pkg)
                return YomkResponse(ResStatus.eOk, "event post success")
            else:
                return YomkResponse(ResStatus.eNo, "event loop not exist")

    def post_wait(self, pkg: Event):
        if not isinstance(pkg, Event):
            raise TypeError("pkg must be Event")
        if not pkg.m_eventLoopName:
            log.error(
                "event loop name is empty, please check Event.m_eventLoopName.")
            return YomkResponse(ResStatus.eNo, "event loop name is empty")
        with self.m_eventLoopMutex.gen_rlock():
            if pkg.m_eventLoopName in self.m_eventLoop:
                self.m_eventLoop[pkg.m_eventLoopName].post_wait(pkg)
                return YomkResponse(ResStatus.eOk, "event post wait success", pkg)
            else:
                return YomkResponse(ResStatus.eNo, "event loop not exist")


class YomkServerInfo(YomkService):
    # 服务表内省服务（对齐 main Modules/ServerInfo）：四端点 /services /functions /function /all；
    # 经 weakref 判活对齐 main m_weakServer.lock() 失败契约（server 销毁后四端点均返 eNo）
    def __init__(self, server: "YomkServer"):
        super().__init__(server)
        # 清除基类对 server 的强引用，仅保留弱引用：
        # 否则本实例存活会连带 server 永不销毁，weakref 判活契约（对齐 main weak_from_this）失效
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
        # 对齐 main m_weakServer.lock()：server 已销毁时返回 None，由各端点统一走 destroyed 契约
        return self.m_weakServer() if self.m_weakServer else None

    def list_services(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list services ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")
        # 对齐 main m_serviceMap（std::map）：服务名字典序输出
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
        # 对齐 main m_funcInfos（std::map）：函数名字典序；行格式 "func [msgName]"（无类型为 "func"）；
        # 不存在的服务返回空表（eOk，对齐 main 现状契约）
        lines = []
        for func_name in sorted(infos.keys()):
            info = infos[func_name]
            lines.append(info.func_name +
                         (f" [{info.msg_name}]" if info.msg_name else ""))
        return YomkResponse(ResStatus.eOk, "ok", lines)

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

        # 命中契约（对齐 main）：eOk 且 msg 精确等于 msgName（非 "ok"），无类型函数 msg 为空
        for name in sorted(server.service_names()):
            if name == srv_name:
                info = server.service_func_infos(srv_name).get(func_name)
                if info is None:
                    log.error("function not found -> " + url)
                    return YomkResponse(ResStatus.eNo, "function not found: " + url)
                return YomkResponse(ResStatus.eOk, info.msg_name)

        log.error("service not found -> " + srv_name)
        return YomkResponse(ResStatus.eNo, "service not found: " + srv_name)

    def list_all(self, pkg):
        server = self._locked_server()
        if server is None:
            log.error("server has been destroyed, list all ignored.")
            return YomkResponse(ResStatus.eNo, "server has been destroyed")

        # 对齐 main：服务名字典序；功能行两空格缩进 + 服务名直拼函数名（函数名自带 / 前缀）
        lines = []
        for srv_name in sorted(server.service_names()):
            lines.append(srv_name)
            infos = server.service_func_infos(srv_name)
            for func_name in sorted(infos.keys()):
                info = infos[func_name]
                lines.append("  " + srv_name + info.func_name +
                             (f" [{info.msg_name}]" if info.msg_name else ""))
        return YomkResponse(ResStatus.eOk, "ok", lines)
