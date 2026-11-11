# /ExtensionService/* 第三方扩展 URL 常量（与框架 src/yomk/url/<域>/ 同构）。
# yomk.url 为纯命名空间层（与 yomk.msg 同构，无 __init__）：扩展自带 yomk/url/extension/
# 叶子子包即可并入框架常量池，与 yomk/url/ctx 等平级共存。
#
# 使用方式（推荐优先）：统一请求接口（server 域链式形态，屏蔽 URL 之下的路由细节）
#   server().url(EXTENSION_ECHO).msg(ExtensionRequest("hello ext")).request()
# 扩展域 builder（extension().version() 等）为可选件，只封装模块自有接口；
# 不提供 builder 的扩展须在此处写明统一请求形态与各常量功能作用。
__all__ = [
    "EXTENSION_ECHO",
    "EXTENSION_VERSION",
]

EXTENSION_ECHO = "/ExtensionService/echo"        # 回显端点：请求包 ExtensionRequest，回包 ExtensionResponse（data 位）
EXTENSION_VERSION = "/ExtensionService/version"  # 版本查询端点：data 位为 "extension v<版本>" 字符串
