try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .builder import ContextBuilder, ContextValueBuilder, GroupSwitch, ctx

__all__ = [
    "__version__",
    "ctx", "ContextBuilder", "ContextValueBuilder", "GroupSwitch",
]
