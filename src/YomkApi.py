from YomkServerPy import (
    YomkServer,
    YomkService,
    YomkResponse,
    ResStatus,
    Context,
    CheckStatus,
    ContextChecker,
    ContextMonitor
)
from typing import Any, Callable

g_server: YomkServer = None

def init(server: type[YomkServer], srv_names: type[list[str]] = []):
    global g_server
    g_server = server
    g_server.start_service(srv_names)

def new_service(class_name: type[YomkService], name: type[str]):
    global g_server
    srv = class_name(g_server)
    srv.set_name(name)
    srv.init()
    g_server.add_service(srv)

def request(url: type[str], pkg: type[Any])->YomkResponse:
    global g_server
    return g_server.request(url, pkg)

def async_request(url: type[str], pkg: type[Any], callback: type[Callable[[YomkResponse], None]])->None:
    global g_server
    return g_server.async_request(url, pkg, callback)

def context_create(key: type[str], value: type[Any])->YomkResponse:
    ctx = Context(key, value)
    return g_server.request("/YomkContext/create", ctx)
    
def context_destroy(key: type[str])->YomkResponse:
    return g_server.request("/YomkContext/destroy", key)

def context_get(key: type[str], default_value: type[Any])->Any:
    ctx = Context(key, default_value)
    res = g_server.request("/YomkContext/get", ctx)
    return res.data

def context_set(key: type[str], default_value: type[Any])->YomkResponse:
    ctx = Context(key, default_value)
    return g_server.request("/YomkContext/set", ctx)

def context_turn_on_checker()->YomkResponse:
    return g_server.request("/YomkContext/turn_on_checker", None)

def context_turn_off_checker()->YomkResponse:
    return g_server.request("/YomkContext/turn_off_checker", None)

def context_set_checker(key: type[str], checker: type[Callable[[Context], CheckStatus]])->YomkResponse:
    contextChecker = ContextChecker(key, checker)
    return g_server.request("/YomkContext/set_checker", contextChecker)

def context_turn_on_monitor()->YomkResponse:
    return g_server.request("/YomkContext/turn_on_monitor", None)

def context_turn_off_monitor()->YomkResponse:
    return g_server.request("/YomkContext/turn_off_monitor", None)

def context_set_monitor(key: type[str], monitor: type[Callable[[Context], None]])->YomkResponse:
    contextMonitor = ContextMonitor(key, monitor)
    return g_server.request("/YomkContext/set_monitor", contextMonitor)
