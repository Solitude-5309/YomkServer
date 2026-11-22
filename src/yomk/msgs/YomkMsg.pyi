from typing import ClassVar, Optional, Type, TypeVar

_T = TypeVar("_T")


class YomkMsg:
    TYPE_NAME: ClassVar[str]

    def __init__(self) -> None: ...


def class_arg_name(cls: object) -> str: ...


def require_msg(v: object, who: str) -> None: ...


def unpack(pkg: object, cls: Type[_T]) -> Optional[_T]: ...


def type_name_of(pkg: object) -> str: ...


def type_mismatch(pkg: object, expected: str) -> str: ...
