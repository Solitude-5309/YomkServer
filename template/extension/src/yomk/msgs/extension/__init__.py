# 第三方扩展 yomk.msgs.extension：消息类型子包（PEP 420 命名空间并入框架 yomk.msgs）
# 独立仓库只写 yomk/msgs/extension/ 目录，安装到框架 site-packages 后与
# yomk/msgs/server、yomk/msgs/ctx 等框架子包平级共存（yomk/msgs 层无 __init__.py，
# 属命名空间段，扩展仓库不得创建 yomk/__init__.py 或 yomk/msgs/__init__.py）
from .ExtensionMsg import ExtensionRequest, ExtensionResponse

__all__ = ["ExtensionRequest", "ExtensionResponse"]
