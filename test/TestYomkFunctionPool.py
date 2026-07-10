import YomkApi
import threading
from typing import Any

def func1(pkg: Any)->YomkApi.YomkResponse:
    tid = threading.get_ident()
    print(f"[thread {tid}] exec func1, with msg:", pkg)
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "exec func1 success")

# 获取线程ID
tid = threading.get_ident()

# 初始化YomkApi
YomkApi.init()

# 注册函数
res = YomkApi.function_pool_register("func1", func1)
print(f"[thread {tid}]", "request: /YomkFunctionPool/func1 response:", res.msg)

# 调用函数
res = YomkApi.function_pool_call("func1", "func1_data")
print(f"[thread {tid}]", "request: /YomkFunctionPool/func1 response:", res.msg)
