# extension 域 builder（与框架各域 builder.py 同构）。
# 扩展 API 形态纪律：
#   1. builder 是模块自己的 API——只封装本模块自有接口，不重复框架通用请求链路；
#   2. builder 为可选件，可以不写；不提供时须在本域 __init__ 或 url 常量池注释中
#      写明统一请求调用形态，并说明各 URL 常量的功能作用，保证使用方无 builder
#      也有文档可循；
#   3. 推荐优先使用框架封装好的统一请求接口（屏蔽 URL 之下的路由细节）：
#        server().url(EXTENSION_ECHO).msg(ExtensionRequest("hello ext")).request()
#      builder 终结仅作模块自有便捷入口。
# 模板接口最小化，仅提供版本查询终结；新增业务终结方法时按同风格在本文件追加
# （内部仍走 _facade.api_request）
from yomk.server import _facade
from yomk.url.extension import EXTENSION_VERSION


class ExtensionBuilder:
    def version(self):
        # 扩展版本查询终结（对齐 C++ ExtensionApi.h extVersion()），返回 Response
        return _facade.api_request(EXTENSION_VERSION, None)


def extension():
    # extension 域链式入口
    return ExtensionBuilder()
