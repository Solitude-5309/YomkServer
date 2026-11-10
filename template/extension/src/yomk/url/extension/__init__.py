# /ExtensionService/* 第三方扩展 URL 常量（与框架 src/yomk/url/<域>/ 同构）。
# yomk.url 为纯命名空间层（与 yomk.msg 同构，无 __init__）：扩展自带 yomk/url/extension/
# 叶子子包即可并入框架常量池，与 yomk/url/ctx 等平级共存。
__all__ = [
    "EXTENSION_ECHO",
]

EXTENSION_ECHO = "/ExtensionService/echo"
