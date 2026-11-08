try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .builder import SrvInfoBuilder, srv_info
from .YomkServerInfo import YomkServerInfo

__all__ = [
    "__version__",
    "srv_info", "SrvInfoBuilder",
    "YomkServerInfo",
]