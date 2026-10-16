import YomkApi
from .version import EXTENSION_VERSION


class ExtensionService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/version", self.version)
        print(
            f"ExtensionService::init install func [ /version ] to {self.get_name()}")
        return 0

    def version(self, pkg):
        # 版本号：EXTENSION_VERSION 常量（对应 main 分支 project(VERSION) 编译期注入的 EXTENSION_VERSION 宏）
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", f"TemplateExtension v{EXTENSION_VERSION}")
