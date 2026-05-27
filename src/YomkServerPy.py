from readerwriterlock import rwlock
from enum import Enum
import logging
from concurrent.futures import ThreadPoolExecutor

logging.basicConfig(
    format="[Yomk] [%(pathname)s:%(lineno)d] [%(funcName)s] %(message)s"
)

log = logging.getLogger(__name__)

class ResStatus(Enum):
    eInvalid = -1
    eOk = 0
    eErr = 1

class YomkResponse:
    def __init__(self, res_status=ResStatus.eInvalid, msg="", data=None):
        self.res_status = res_status
        self.msg = msg
        self.data = data

class YomkService:
    def __init__(self, server):
        self.name = ""
        self.server = server
        self.functions = {}
        self.rwlock_functions = rwlock.RWLockFair()
    
    def get_name(self):
        return self.name
    
    def set_name(self, name):
        self.name = name
    
    def init(self):
        pass
    
    def install_func(self, name, function):
        with self.rwlock_functions.gen_wlock():
            if (name in self.functions):
                log.info(f"install function already exists -> {name}, update to current function")
            self.functions[name] = function
    
    def invoke(self, name, pkg):
        func = None
        with self.rwlock_functions.gen_rlock():
            if (name not in self.functions):
                log.error(f"function not found -> {name}, please use YomkInstallFunc to install this function.")
                return YomkResponse(ResStatus.eErr, "function not found: " + name)
            func = self.functions[name]
        return func(pkg)

class YomkServer:
    def __init__(self, max_thread=8):
        self.services = {}
        self.rwlock_services = rwlock.RWLockFair()
        self.executor = ThreadPoolExecutor(max_thread)
    
    def start_service(self, srv_names):
        for srv_name in srv_names:
            if srv_name == "YomkContext":
                srv = YomkContext(self)
                srv.set_name("/YomkContext")
                srv.init()
                self.add_service(srv)
            else:
                log.info(f"yomk does not support service: {srv_name}")
    
    def add_service(self, service):
        with self.rwlock_services.gen_wlock():
            if (service.name in self.services):
                log.info(f"add service already exists -> {service.name}, update to current service")
            self.services[service.name] = service
    
    def request(self, url, pkg):
        if not url.startswith("/"):
            log.error(f"url parse error: {url}, please start with /")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", please start with /"
            )
        
        pos_start = 0
        pos_end = url.find("/", pos_start + 1)
        if pos_end == -1:
            log.error(f"url parse error: {url}, not found service name.")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", not found service name."
            )
        
        srv_name = url[pos_start:pos_end]
        if not srv_name:
            log.error(f"url parse error: srv is empty.")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: srv is empty."
            )
        
        func_name = url[pos_end:]
        if not func_name:
            log.error(f"url parse error: function name is empty")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: function name is empty"
            )        
        
        service = None
        with self.rwlock_services.gen_rlock():
            if (srv_name not in self.services):
                log.error(f"service not found: {srv_name}, please start the service.")
                return YomkResponse(
                    ResStatus.eErr,
                    "service not found: " + srv_name
                )
            service = self.services[srv_name]
        
        return service.invoke(func_name, pkg)
    
    def async_request(self, url, pkg, callback):
        if not url.startswith("/"):
            log.error(f"url parse error: {url}, please start with /")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", please start with /"
            )
        
        pos_start = 0
        pos_end = url.find("/", pos_start + 1)
        if pos_end == -1:
            log.error(f"url parse error: {url}, not found service name.")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", not found service name."
            )
        
        srv_name = url[pos_start:pos_end]
        if not srv_name:
            log.error(f"url parse error: srv is empty.")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: srv is empty."
            )
        
        func_name = url[pos_end:]
        if not func_name:
            log.error(f"url parse error: function name is empty")
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: function name is empty"
            )        
        
        service = None
        with self.rwlock_services.gen_rlock():
            if (srv_name not in self.services):
                log.error(f"service not found: {srv_name}, please start the service.")
                return YomkResponse(
                    ResStatus.eErr,
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

# modules
class YomkContext(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkContext")
        self.m_contexts = {}
        self.m_contextsMutex = rwlock.RWLockFair()

    def init(self):
        self.install_func("/create", self.create)
        self.install_func("/destroy", self.destroy)
        self.install_func("/get", self.get)
        self.install_func("/set", self.set)

    def create(self, ctx):
        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eErr, "key is empty")

        with self.m_contextsMutex.gen_wlock():
            if ctx.key in self.m_contexts:
                log.error(f"key already exists: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eErr, "key already exists")
            self.m_contexts[ctx.key] = ctx.value
        return YomkResponse(ResStatus.eOk, "create context success")
    
    def destroy(self, key):
        if not key:
            log.error("key is empty, please check key.")
            return YomkResponse(ResStatus.eErr, "key is empty")
            
        with self.m_contextsMutex.gen_wlock():
            if key not in self.m_contexts:
                log.error(f"key is not exist: {key}, please check key.")
                return YomkResponse(ResStatus.eErr, "key is not exist")
            del self.m_contexts[key]
        return YomkResponse(ResStatus.eOk, "destroy context success")
    
    def get(self, ctx):
        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eErr, "key is empty", ctx.value)
        
        with self.m_contextsMutex.gen_rlock():
            if ctx.key not in self.m_contexts:
                log.error(f"key is not exist: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eErr, "key is not exist", ctx.value)
        
        return YomkResponse(ResStatus.eOk, "get context success", self.m_contexts[ctx.key])
    
    def set(self, ctx):
        if not ctx.key:
            log.error("key is empty, please check Context.m_key.")
            return YomkResponse(ResStatus.eErr, "key is empty")
        
        with self.m_contextsMutex.gen_wlock():
            if ctx.key not in self.m_contexts:
                log.error(f"key is not exist: {ctx.key}, please check Context.m_key.")
                return YomkResponse(ResStatus.eErr, "key is not exist")
            self.m_contexts[ctx.key] = ctx.value
        return YomkResponse(ResStatus.eOk, "set context success")
