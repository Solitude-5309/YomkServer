# 扩展运行时服务（第三方运行时扩展示例）
# 纪律：第三方服务不做隐式发现——使用方须显式 server().srv(ExtensionService).create()
# 注册（见 test/test_extension.py）；框架只拉起 5 个内置服务。
# 调用方推荐优先使用框架封装好的统一请求接口访问本服务：
#   server().url(EXTENSION_ECHO).msg(ExtensionRequest("hello ext")).request()
from yomk.server import YomkService
from yomk.msg.server import ResStatus, Response
from yomk.msg.extension import ExtensionRequest, ExtensionResponse
from yomk.log import log


class ExtensionService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/ExtensionService")

    def init(self):
        self.install_func("/echo", self.echo)
        self.install_func("/version", self.version)
        log().tag("ExtensionService::init").info(
            "install func [ /echo /version ] to ", self.get_name())
        return 0

    def echo(self, pkg):
        # 入参 pkg 为注册消息类型的实例（消息包/持有语义：框架不拷贝）；
        # 返回框架 Response，强类型回包以 ExtensionResponse 承载在 data 位
        if not isinstance(pkg, ExtensionRequest):
            return Response(ResStatus.eNo, "pkg type mismatch, ExtensionRequest expected")
        return Response(ResStatus.eOk, "echo",
                        ExtensionResponse("ok", "echo", pkg.payload))

    def version(self, pkg):
        # 版本查询终结（对齐 C++ ExtensionService::version）：版本号单一来源为
        # yomk.extension.__init__.__version__，调用期延迟导入避开循环 import
        from yomk.extension import __version__
        return Response(ResStatus.Succeeded, "ok", f"extension v{__version__}")
