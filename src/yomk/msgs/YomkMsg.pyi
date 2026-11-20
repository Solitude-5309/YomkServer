from typing import Optional, Type, TypeVar

_T = TypeVar("_T")


class YomkMsg:
    kName: str

    def __init__(self) -> None: ...


def unpack(pkg: object, cls: Type[_T]) -> Optional[_T]: ...
