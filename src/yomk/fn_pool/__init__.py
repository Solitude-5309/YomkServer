try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .YomkFunctionPool import YomkFunctionPool

__all__ = ["__version__", "YomkFunctionPool"]