try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .YomkBoot import YomkBoot

__all__ = [
    "__version__",
    "YomkBoot",
]
