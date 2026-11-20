# 第三方扩展 yomk.msgs.extension：消息类型子包（并入框架 yomk.msgs 包）
# 独立仓库只写 yomk/msgs/extension/ 目录，安装到框架 site-packages 后与
# yomk/msgs/server、yomk/msgs/ctx 等框架子包平级共存（框架 yomk/msgs 层有
# __init__.py——标准工具层顶层导出；扩展仓库不得创建或覆盖 yomk/__init__.py
# 与 yomk/msgs/__init__.py）
from .ExtensionMsg import ExtensionRequest, ExtensionResponse

__all__ = ["ExtensionRequest", "ExtensionResponse"]
