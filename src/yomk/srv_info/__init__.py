try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .YomkServerInfo import YomkServerInfo

__all__ = ["__version__", "YomkServerInfo"]