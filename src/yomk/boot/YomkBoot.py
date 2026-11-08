# 启动编排基类（独立 boot 域：服务生命周期钩子契约，不承载链式入口）
# 使用方派生 YomkBoot 实现 before/start/after 三阶段钩子，经
# server().boot_ptr(boot).run() 驱动（见 yomk.server 域）。
from abc import ABC, abstractmethod


class YomkBoot(ABC):
    # 启动编排钩子：before -> start -> after，未显式返回等价成功
    def __init__(self) -> None:
        pass

    @abstractmethod
    def before(self) -> int:
        pass

    @abstractmethod
    def start(self) -> int:
        pass

    @abstractmethod
    def after(self) -> int:
        pass
