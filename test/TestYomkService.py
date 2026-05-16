import YomkApi

class YomkServiceA(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkServiceA")

    def init(self):
        self.install_func("/skill_a", self.skill_a)

    def skill_a(self, pkg):
        print("YomkServiceA::callSkillA", self.get_name(), "exec skill a, with msg:", pkg)
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, self.get_name() + " exec skill a success")

YomkApi.init(YomkApi.YomkServer())
YomkApi.new_service(YomkServiceA, "/YomkServiceA")
YomkApi.request("/YomkServiceA/skill_a", "hello")
