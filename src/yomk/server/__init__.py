try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .YomkServer import YomkServer
from .YomkService import YomkService

__all__ = ["__version__", "YomkServer", "YomkService"]