# 第三方扩展 yomk.extension：运行时域子包（PEP 420 命名空间并入框架 yomk）
# 与框架 yomk/server、yomk/ctx 等域平级；扩展仓库不得创建 yomk/__init__.py
# 版本常量须先于子模块导入（ExtensionService.version 延迟引用本模块 __version__）
__version__ = "0.0.1"

from .ExtensionService import ExtensionService
from .builder import ExtensionBuilder, extension

__all__ = [
    "__version__",
    "extension", "ExtensionBuilder",
    "ExtensionService",
]
