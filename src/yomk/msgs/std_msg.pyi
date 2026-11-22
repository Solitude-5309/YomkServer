from yomk.msgs.YomkMsg import YomkMsg as YomkMsg


class Bool(YomkMsg):
    value: bool

    def __init__(self, value: bool = False) -> None: ...


class Int(YomkMsg):
    value: int

    def __init__(self, value: int = 0) -> None: ...


class Float(YomkMsg):
    value: float

    def __init__(self, value: float = 0.0) -> None: ...


class String(YomkMsg):
    value: str

    def __init__(self, value: str = "") -> None: ...


class Bytes(YomkMsg):
    value: bytes

    def __init__(self, value: bytes = b"") -> None: ...


class BoolArray(YomkMsg):
    items: list[bool]

    def __init__(self, items: list[bool] | None = None) -> None: ...


class IntArray(YomkMsg):
    items: list[int]

    def __init__(self, items: list[int] | None = None) -> None: ...


class FloatArray(YomkMsg):
    items: list[float]

    def __init__(self, items: list[float] | None = None) -> None: ...


class StringArray(YomkMsg):
    items: list[str]

    def __init__(self, items: list[str] | None = None) -> None: ...
