from typing import ClassVar, Generic, Optional, Type, TypeGuard, TypeVar

_T = TypeVar("_T")
_U = TypeVar("_U")


class YomkMsg:
    TYPE_NAME: ClassVar[str]

    def __init__(self) -> None: ...


class UnpackResult(Generic[_U]):
    msg: Optional[_U]
    error: str

    def __bool__(self) -> bool: ...


def is_msg_instance(v: object) -> TypeGuard[YomkMsg]: ...


def is_msg_subclass(cls: object) -> TypeGuard[Type[YomkMsg]]: ...


def require_msg_instance(v: object, who: str) -> None: ...


def require_msg_subclass(cls: object, who: str) -> None: ...


def unpack(msg: object, cls: Type[_T]) -> UnpackResult[_T]: ...

