import YomkApi
import threading
import sys
from pathlib import Path
current_dir = Path(__file__).resolve()
sys.path.append(str(current_dir))

from boot.MyBoot import MyBoot
from msgs.YomkMsgs import MyServiceMsg

def callback(res: YomkApi.YomkResponse)->None:
    tid = threading.get_ident()
    print(f"[thread {tid}]", "async_request_res", res.msg)

# 获取线程ID
tid = threading.get_ident()

# 初始化YomkApi
YomkApi.boot(MyBoot(["/YomkServiceA"]))

# 同步请求
res = YomkApi.request("/YomkServiceA/skill_a", MyServiceMsg("hello"))
print(f"[thread {tid}]", "request: /YomkServiceA/skill_a response:", res.msg)

# 异步请求
YomkApi.async_request("/YomkServiceA/skill_a", MyServiceMsg("hello"), callback)
print(f"[thread {tid}]", "async_request: /YomkServiceA/skill_a finished. ")
