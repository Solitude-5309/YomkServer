from readerwriterlock import rwlock
from enum import Enum
import logging
import queue
import threading
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
                log.info(
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
    def __init__(self, max_thread=8):
        self.services: dict[str, YomkService] = {}
        self.rwlock_services = rwlock.RWLockFair()
        self.executor = ThreadPoolExecutor(max_thread)

    def start_service(self, srv_names: list[str]):
        for srv_name in srv_names:
            if srv_name == "YomkContext":
                srv = YomkContext(self)
                srv.set_name("/YomkContext")
                srv.init()
                self.add_service(srv)
            elif srv_name == "YomkFunctionPool":
                srv = YomkFunctionPool(self)
                srv.set_name("/YomkFunctionPool")
                srv.init()
                self.add_service(srv)
            elif srv_name == "YomkEventLoop":
                srv = YomkEventLoop(self)
                srv.set_name("/YomkEventLoop")
                srv.init()
                self.add_service(srv)
            else:
                log.info(f"yomk not support service: {srv_name}")

    def add_service(self, service):
        if not isinstance(service, YomkService):
            raise TypeError("service must be YomkService")

        with self.rwlock_services.gen_wlock():
            if (service.get_name() in self.services):
                log.info(
                    f"add service already exists -> {service.get_name()}, update to current service")
            self.services[service.get_name()] = service
            return 0

    def request(self, url, pkg):
        if not url.startswith("/"):
            log.error(f"url parse error: {url}, please start with /")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: " + url + ", please start with /"
            )

        pos_start = 0
        pos_end = url.find("/", pos_start + 1)
        if pos_end == -1:
            log.error(f"url parse error: {url}, not found service name.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: " + url + ", not found service name."
            )

        srv_name = url[pos_start:pos_end]
        if not srv_name.startswith("/"):
            log.error(f"url parse error: srv name not start with /.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: srv name not start with /."
            )

        func_name = url[pos_end:]
        if not func_name.startswith("/"):
            log.error(f"url parse error: function name not start with /.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: function name not start with /."
            )

        with self.rwlock_services.gen_rlock():
            if (srv_name not in self.services):
                log.error(
                    f"service not found: {srv_name}, please start the service.")
                return YomkResponse(
                    ResStatus.eNo,
                    "service not found: " + srv_name
                )
            service = self.services[srv_name]

        return service.invoke(func_name, pkg)

    def async_request(self, url, pkg, callback):
        if not url.startswith("/"):
            log.error(f"url parse error: {url}, please start with /")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: " + url + ", please start with /"
            )

        pos_start = 0
        pos_end = url.find("/", pos_start + 1)
        if pos_end == -1:
            log.error(f"url parse error: {url}, not found service name.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: " + url + ", not found service name."
            )

        srv_name = url[pos_start:pos_end]
        if not srv_name.startswith("/"):
            log.error(f"url parse error: srv name not start with /.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: srv name not start with /."
            )

        func_name = url[pos_end:]
        if not func_name.startswith("/"):
            log.error(f"url parse error: function name not start with /.")
            return YomkResponse(
                ResStatus.eNo,
                "url parse error: function name not start with /."
            )

        with self.rwlock_services.gen_rlock():
            if (srv_name not in self.services):
                log.error(
                    f"service not found: {srv_name}, please start the service.")
                return YomkResponse(
                    ResStatus.eNo,
                    "service not found: " + srv_name
                )
            service = self.services[srv_name]

        def task():
            try:
                result = service.invoke(func_name, pkg)
                callback(result)
            except Exception as e:
                log.exception(e)

        self.executor.submit(task)

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


class YomkContext(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_contexts = {}
        self.m_contextsMutex = rwlock.RWLockFair()
        self.m_checkerEnabled = False
        self.m_checkers = {}
        self.m_checkersMutex = rwlock.RWLockFair()
        self.m_monitorEnabled = False
        self.m_monitors = {}
        self.m_monitorsMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/create", self.create)
        self.install_func("/destroy", self.destroy)
        self.install_func("/get", self.get)
        self.install_func("/set", self.set)
        self.install_func("/turn_on_checker", self.turn_on_checker)
        self.install_func("/turn_off_checker", self.turn_off_checker)
        self.install_func("/set_checker", self.set_checker)
        self.install_func("/turn_on_monitor", self.turn_on_monitor)
        self.install_func("/turn_off_monitor", self.turn_off_monitor)
        self.install_func("/set_monitor", self.set_monitor)

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
            return YomkResponse(ResStatus.eNo, "monitor_func is empty")

        with self.m_contextsMutex.gen_rlock():
            if contextMonitor.key not in self.m_contexts:
                log.info(
                    f"YomkContext key: {contextMonitor.key} is not exist, please check ContextMonitor.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")

        with self.m_monitorsMutex.gen_wlock():
            self.m_monitors.setdefault(
                contextMonitor.key, []
            ).append(Monitor(contextMonitor.monitor_func, contextMonitor.async_monitor_func))

        return YomkResponse(ResStatus.eOk, "set monitor success")

    def set_checker(self, contextChecker):
        if not isinstance(contextChecker, ContextChecker):
            raise TypeError("contextChecker must be ContextChecker")

        if not contextChecker.key:
            log.error("key is empty, please check ContextChecker.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        if not contextChecker.check_func:
            log.error(
                "check_func is empty, please check ContextChecker.m_check_func.")
            return YomkResponse(ResStatus.eNo, "check_func is empty")

        with self.m_contextsMutex.gen_rlock():
            if contextChecker.key not in self.m_contexts:
                log.info(
                    f"YomkContext key: {contextChecker.key} is not exist, please check ContextChecker.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")

        with self.m_checkersMutex.gen_wlock():
            self.m_checkers[contextChecker.key] = contextChecker.check_func

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

        with self.m_contextsMutex.gen_wlock():
            if ctx.key in self.m_contexts:
                log.error(
                    f"key already exists: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eNo, "key already exists")
            self.m_contexts[ctx.key] = ctx.value
        return YomkResponse(ResStatus.eOk, "create context success")

    def destroy(self, key):
        if not key:
            log.error("key is empty, please check key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

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

        return YomkResponse(ResStatus.eOk, "get context success", self.m_contexts[ctx.key])

    def set(self, ctx):
        if not isinstance(ctx, Context):
            raise TypeError("ctx must be Context")

        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eNo, "key is empty")

        with self.m_contextsMutex.gen_wlock():
            if ctx.key not in self.m_contexts:
                log.error(
                    f"key is not exist: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eNo, "key is not exist")

            if self.m_checkerEnabled:
                with self.m_checkersMutex.gen_rlock():
                    if ctx.key in self.m_checkers:
                        if (self.m_checkers[ctx.key](ctx) == CheckStatus.eReject):
                            return YomkResponse(ResStatus.eNo, "check reject set context")

            self.m_contexts[ctx.key] = ctx.value

        if self.m_monitorEnabled:
            with self.m_monitorsMutex.gen_rlock():
                if ctx.key in self.m_monitors:
                    for monitor in self.m_monitors[ctx.key]:
                        if (monitor.async_monitor_func):
                            t = threading.Thread(
                                target=monitor.monitor_func, args=(ctx,))
                            t.start()
                        else:
                            monitor.monitor_func(ctx)

        return YomkResponse(ResStatus.eOk, "set context success")


class YomkFunctionPool(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkFunctionPool")
        self.m_functions = {}
        self.m_functionsMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/register", self.registerFunction)
        self.install_func("/call", self.callFunction)

    def registerFunction(self, function: Function):
        if not isinstance(function, Function):
            raise TypeError("function must be Function")

        if not function.name:
            log.error("funcName is empty, please check Function.m_funcName.")
            return YomkResponse(ResStatus.eNo, "funcName is empty")

        if not function.func:
            log.error("func is empty, please check Function.m_func.")
            return YomkResponse(ResStatus.eNo, "func is empty")

        with self.m_functionsMutex.gen_wlock():
            if function.name in self.m_functions:
                log.info(
                    f"YomkFunctionPool function name: {function.name} is already exist, and update current function")

            self.m_functions[function.name] = function.func

        return YomkResponse(ResStatus.eOk, "register function success")

    def callFunction(self, call_func: CallFunction):
        if not isinstance(call_func, CallFunction):
            raise TypeError("call_func must be CallFunction")

        if not call_func.name:
            log.error("funcName is empty, please check CallFunction.m_funcName.")
            return YomkResponse(ResStatus.eNo, "funcName is empty")

        with self.m_functionsMutex.gen_rlock():
            if call_func.name not in self.m_functions:
                log.error(
                    f"funcName is not exist: {call_func.name}, please check CallFunction.m_funcName.")
                return YomkResponse(ResStatus.eNo, "funcName is not exist")
            else:
                func = self.m_functions[call_func.name]

        return func(call_func.pkg)


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
                log.info("EventLoop: event is null, please check event")
                continue
            try:
                event.handle()
                if event.m_waitCallback:
                    event.m_waitCallback()
            except Exception as e:
                log.error(f"Event handling failed: {e}")

    def start(self):
        if self.m_running:
            return
        self.m_running = True
        self.m_thread = threading.Thread(target=self.run)
        self.m_thread.start()
        return

    def stop(self):
        if not self.m_running:
            return
        self.m_running = False
        with self.m_queueMutex:
            while not self.m_event_queue.empty():
                self.m_event_queue.get_nowait()
            self.m_condition.notify_all()
        if self.m_thread and self.m_thread.is_alive():
            self.m_thread.join()
        return 0

    def post(self, event):
        if not event:
            log.warning("EventLoop: post event is null")
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
        self.install_func("/start", self.start)
        self.install_func("/stop", self.stop)
        self.install_func("/destroy", self.destroy)
        self.install_func("/post", self.post)
        self.install_func("/post_wait", self.post_wait)

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
