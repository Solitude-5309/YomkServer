from abc import ABC, abstractmethod


class YomkBoot(ABC):
    # 启动编排钩子：before -> start -> after，未显式返回等价成功
    def __init__(self) -> None: ...

    @abstractmethod
    def before(self) -> int: ...

    @abstractmethod
    def start(self) -> int: ...

    @abstractmethod
    def after(self) -> int: ...
