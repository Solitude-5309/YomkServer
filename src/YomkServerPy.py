from readerwriterlock import rwlock
from enum import Enum

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
            self.functions[name] = function
    
    def invoke(self, name, pkg):
        with self.rwlock_functions.gen_rlock():
            return self.functions[name](pkg)

class YomkServer:
    def __init__(self):
        self.services = {}
        self.rwlock_services = rwlock.RWLockFair()
    
    def add_service(self, service):
        with self.rwlock_services.gen_wlock():
            self.services[service.name] = service
    
    def request(self, url, pkg):
        if not url.startswith("/"):
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", please start with /"
            )
        
        pos_start = 0
        pos_end = url.find("/", pos_start + 1)
        if pos_end == -1:
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: " + url + ", not found service name."
            )
        
        srv_name = url[pos_start:pos_end]
        if not srv_name:
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: srv is empty."
            )
        
        func_name = url[pos_end:]
        if not func_name:
            return YomkResponse(
                ResStatus.eErr,
                "url parse error: function name is empty"
            )        
        
        with self.rwlock_services.gen_rlock():
            service = self.services[srv_name]
            return service.invoke(func_name, pkg)
