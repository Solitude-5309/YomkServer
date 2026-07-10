import YomkApi
import threading
from typing import Any

g_count: int = 0

def event_handle(pkg: Any)->YomkApi.YomkResponse:
    tid = threading.get_ident()
    print(f"[thread {tid}] exec event_handle, with msg:", pkg)
    global g_count
    g_count += 1
    if g_count < 4:
        YomkApi.event_loop_post("event_loop_1", "requestEventHandle_data_" + str(g_count))
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "exec event_handle success")

# 获取线程ID
tid = threading.get_ident()

# 初始化YomkApi
YomkApi.init()

# 开启事件循环
res = YomkApi.event_loop_start("event_loop_1", event_handle)
print(f"[thread {tid}]", "request: /YomkEventLoop/start response:", res.msg)

# 异步投递事件
res = YomkApi.event_loop_post("event_loop_1", "requestEventHandle_data")
print(f"[thread {tid}]", "request: /YomkEventLoop/post response:", res.msg)

# 同步投递事件
res = YomkApi.event_loop_post_wait("event_loop_1", "requestEventHandle_data_wait")
print(f"[thread {tid}]", "request: /YomkEventLoop/post_wait response:", res.msg)
print(f"[thread {tid}]", "request: /YomkEventLoop/post_wait response:", "event id: ", res.data.m_eventId, "event loop name: ", res.data.m_eventLoopName, "res msg: ", res.data.m_response.msg)

# 停止事件循环
input("press any key to stop event loop: ")
res = YomkApi.event_loop_stop("event_loop_1")
print(f"[thread {tid}]", "request: /YomkEventLoop/stop response:", res.msg)

# 销毁事件循环
input("press any key to destroy event loop: ")
res = YomkApi.event_loop_destroy("event_loop_1")
print(f"[thread {tid}]", "request: /YomkEventLoop/destroy response:", res.msg)
