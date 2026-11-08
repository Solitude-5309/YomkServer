try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .builder import EventLoopBuilder, event_loop
from .EventLoop import Event, EventLoop
from .YomkEventLoop import YomkEventLoop

__all__ = [
    "__version__",
    "event_loop", "EventLoopBuilder",
    "Event", "EventLoop", "YomkEventLoop",
]