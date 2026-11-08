# 第三方扩展 yomk.msg.extension_service：消息类型子包（PEP 420 命名空间并入框架 yomk.msg）
# 独立仓库只写 yomk/msg/extension_service/ 目录，安装到框架 site-packages 后与
# yomk/msg/server、yomk/msg/ctx 等框架子包平级共存（yomk/msg 层无 __init__.py，
# 属命名空间段，扩展仓库不得创建 yomk/__init__.py 或 yomk/msg/__init__.py）
from .ExtensionMsg import ExtensionRequest, ExtensionResponse

__all__ = ["ExtensionRequest", "ExtensionResponse"]
