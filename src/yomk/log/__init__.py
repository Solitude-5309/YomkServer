try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .ConsoleLogger import ConsoleLogger
from .FileLogger import FileLogger
from .YomkLogger import YomkLogger

__all__ = ["__version__", "ConsoleLogger", "FileLogger", "YomkLogger"]