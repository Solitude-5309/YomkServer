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
        print(f"[thread {tid}] hello", "YomkServiceA::callSkillA", self.get_name(), "exec skill a, with msg:", pkg.msg)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, self.get_name() + " exec skill a success")