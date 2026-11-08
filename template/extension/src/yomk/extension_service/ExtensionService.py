# 扩展运行时服务（第三方运行时扩展示例）
# 纪律：第三方服务不做隐式发现——使用方须显式 server().srv(ExtensionService).create()
# 注册（见 test/test_template_extension.py）；框架只拉起 5 个内置服务
from yomk.server import YomkService
from yomk.msg.server import ResStatus, Response
from yomk.msg.extension_service import ExtensionRequest, ExtensionResponse
from yomk.log import log


class ExtensionService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/echo", self.echo)
        log().tag("ExtensionService::init").info("install func [ /echo ] to ", self.get_name())
        return 0

    def echo(self, pkg):
        # 入参 pkg 为注册消息类型的实例（消息包/持有语义：框架不拷贝）；
        # 返回框架 Response，强类型回包以 ExtensionResponse 承载在 data 位
        if not isinstance(pkg, ExtensionRequest):
            return Response(ResStatus.eNo, "pkg type mismatch, ExtensionRequest expected")
        return Response(ResStatus.eOk, "echo",
                        ExtensionResponse("ok", "echo", pkg.payload))
