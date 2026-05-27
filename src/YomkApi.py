from YomkServerPy import (
    YomkServer,
    YomkService,
    YomkResponse,
    ResStatus,
    Context
)

g_server: YomkServer = None

def init(server, srv_names=[]):
    global g_server
    g_server = server
    g_server.start_service(srv_names)

def new_service(class_name, name):
    global g_server
    srv = class_name(g_server)
    srv.set_name(name)
    srv.init()
    g_server.add_service(srv)

def request(url, pkg):
    global g_server
    return g_server.request(url, pkg)

def async_request(url, pkg, callback):
    global g_server
    return g_server.async_request(url, pkg, callback)

def context_create(key, value):
    ctx = Context(key, value)
    return g_server.request("/YomkContext/create", ctx)
    
def context_destroy(key):
    return g_server.request("/YomkContext/destroy", key)

def context_get(key, default_value):
    ctx = Context(key, default_value)
    res = g_server.request("/YomkContext/get", ctx)
    return res.data

def context_set(key, default_value):
    ctx = Context(key, default_value)
    return g_server.request("/YomkContext/set", ctx)
