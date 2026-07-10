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
    Event
)
from typing import Any, Callable

g_server: YomkServer = YomkServer()

def init():
    global g_server
    g_server.start_service(["YomkContext", "YomkFunctionPool", "YomkEventLoop"])
    return g_server

def server():
    global g_server
    return g_server

def new_service(class_name: type[YomkService], name: type[str] = ""):
    global g_server
    srv = class_name(g_server)
    srv.set_name(name)
    srv.init()
    g_server.add_service(srv)
    
def add_service(srv: type[YomkService], name: type[str] = ""):
    global g_server
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
    global g_server
    ctx = Context(key, value)
    return g_server.request("/YomkContext/create", ctx)
    
def context_destroy(key: type[str])->YomkResponse:
    global g_server
    return g_server.request("/YomkContext/destroy", key)

def context_get(key: type[str], default_value: type[Any])->Any:
    global g_server
    ctx = Context(key, default_value)
    res = g_server.request("/YomkContext/get", ctx)
    return res.data

def context_set(key: type[str], default_value: type[Any])->YomkResponse:
    global g_server
    ctx = Context(key, default_value)
    return g_server.request("/YomkContext/set", ctx)

def context_turn_on_checker()->YomkResponse:
    global g_server
    return g_server.request("/YomkContext/turn_on_checker", None)

def context_turn_off_checker()->YomkResponse:
    global g_server
    return g_server.request("/YomkContext/turn_off_checker", None)

def context_set_checker(key: type[str], checker: type[Callable[[Context], CheckStatus]])->YomkResponse:
    global g_server
    contextChecker = ContextChecker(key, checker)
    return g_server.request("/YomkContext/set_checker", contextChecker)

def context_turn_on_monitor()->YomkResponse:
    global g_server
    return g_server.request("/YomkContext/turn_on_monitor", None)

def context_turn_off_monitor()->YomkResponse:
    global g_server
    return g_server.request("/YomkContext/turn_off_monitor", None)

def context_set_monitor(key: type[str], monitor: type[Callable[[Context], None]])->YomkResponse:
    global g_server
    contextMonitor = ContextMonitor(key, monitor)
    return g_server.request("/YomkContext/set_monitor", contextMonitor)

def function_pool_register(name: type[str], func: type[Callable[[Any], YomkResponse]])->YomkResponse:
    global g_server
    return g_server.request("/YomkFunctionPool/register", Function(name, func))

def function_pool_call(name: type[str], pkg: type[Any])->YomkResponse:
    global g_server
    return g_server.request("/YomkFunctionPool/call", CallFunction(name, pkg))

def event_loop_start(event_loop_name: type[str], callback: type[Callable[[Any], YomkResponse]]=None)->YomkResponse:
    global g_server
    return g_server.request("/YomkEventLoop/start", EventLoopPkg(event_loop_name, callback))

def event_loop_stop(event_loop_name: type[str])->YomkResponse:
    global g_server
    return g_server.request("/YomkEventLoop/stop", event_loop_name)

def event_loop_destroy(event_loop_name: type[str])->YomkResponse:
    global g_server
    return g_server.request("/YomkEventLoop/destroy", event_loop_name)

def event_loop_post(event_loop_name: type[str], pkg: type[Any], callback: type[Callable[[Any], YomkResponse]]=None)->YomkResponse:
    global g_server
    return g_server.request("/YomkEventLoop/post", Event(event_loop_name, pkg, callback))

def event_loop_post_wait(event_loop_name: type[str], pkg: type[Any], callback: type[Callable[[Any], YomkResponse]]=None)->YomkResponse:
    global g_server
    return g_server.request("/YomkEventLoop/post_wait", Event(event_loop_name, pkg, callback))
