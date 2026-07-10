import YomkApi
import threading
from typing import Any

class YomkServiceA(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkServiceA")

    def init(self):
        self.install_func("/skill_a", self.skill_a)

    def skill_a(self, pkg: Any)->YomkApi.YomkResponse:
        tid = threading.get_ident()
        print(f"[thread {tid}] hello", "YomkServiceA::callSkillA", self.get_name(), "exec skill a, with msg:", pkg)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, self.get_name() + " exec skill a success")

def callback(res: YomkApi.YomkResponse)->None:
    tid = threading.get_ident()
    print(f"[thread {tid}]", "async_request_res", res.msg)

# 获取线程ID
tid = threading.get_ident()

# 初始化YomkApi
YomkApi.init()

# 创建服务
YomkApi.new_service(YomkServiceA)

# 同步请求
res = YomkApi.request("/YomkServiceA/skill_a", "hello")
print(f"[thread {tid}]", "request: /YomkServiceA/skill_a response:", res.msg)

# 异步请求
YomkApi.async_request("/YomkServiceA/skill_a", "hello", callback)
print(f"[thread {tid}]", "async_request: /YomkServiceA/skill_a finished. ")
