from abc import ABC, abstractmethod


class YomkBoot(ABC):
    def __init__(self) -> None: ...

    @abstractmethod
    def before(self) -> int: ...

    @abstractmethod
    def start(self) -> int: ...

    @abstractmethod
    def after(self) -> int: ...
