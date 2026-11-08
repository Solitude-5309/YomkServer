try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .builder import FnPoolBuilder, fn_pool
from .YomkFunctionPool import YomkFunctionPool

__all__ = [
    "__version__",
    "fn_pool", "FnPoolBuilder",
    "YomkFunctionPool",
]