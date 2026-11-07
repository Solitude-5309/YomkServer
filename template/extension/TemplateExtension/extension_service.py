import YomkApi
from .version import EXTENSION_VERSION
from YomkApi import log


class ExtensionService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/version", self.version)
        log().tag("ExtensionService::init").info("install func [ /version ] to ", self.get_name())
        return 0

    def version(self, pkg):
        # 版本号：EXTENSION_VERSION 常量（定义于 version.py）
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "ok", f"TemplateExtension v{EXTENSION_VERSION}")
