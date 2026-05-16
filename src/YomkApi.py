from YomkServerPy import (
    YomkServer,
    YomkService,
    YomkResponse,
    ResStatus,
)

g_server: YomkServer = None

def init(server):
    global g_server
    g_server = server

def new_service(class_name, name):
    global g_server
    srv = class_name(g_server)
    srv.set_name(name)
    srv.init()
    g_server.add_service(srv)

def request(url, pkg):
    global g_server
    return g_server.request(url, pkg)
