from typing import ClassVar, Optional, Type, TypeGuard, TypeVar

_T = TypeVar("_T")


class YomkMsg:
    TYPE_NAME: ClassVar[str]

    def __init__(self) -> None: ...


def is_msg_payload(v: object) -> TypeGuard[Optional[YomkMsg]]: ...


def is_msg_subclass(cls: object) -> TypeGuard[Type[YomkMsg]]: ...


def require_msg_payload(v: object, who: str) -> None: ...


def require_msg_subclass(cls: object, who: str) -> None: ...


def unpack(msg: object, cls: Type[_T]) -> Optional[_T]: ...


def type_mismatch(msg: object, expected: str) -> str: ...
