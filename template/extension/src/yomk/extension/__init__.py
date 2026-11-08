# 第三方扩展 yomk.extension：运行时域子包（PEP 420 命名空间并入框架 yomk）
# 与框架 yomk/server、yomk/ctx 等域平级；扩展仓库不得创建 yomk/__init__.py
from .ExtensionService import ExtensionService

# 扩展版本号（单一来源：本文件）
__version__ = "0.0.1"

__all__ = ["ExtensionService"]
