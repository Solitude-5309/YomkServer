try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .EventLoop import Event, EventLoop
from .YomkEventLoop import YomkEventLoop

__all__ = ["__version__", "Event", "EventLoop", "YomkEventLoop"]