from typing import ClassVar, Optional, Type, TypeVar

_T = TypeVar("_T")


class YomkMsg:
    TYPE_NAME: ClassVar[str]

    def __init__(self) -> None: ...


def unpack(pkg: object, cls: Type[_T]) -> Optional[_T]: ...
