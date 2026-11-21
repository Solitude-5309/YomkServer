"""Boot orchestration base class for the before -> start -> after hooks.

启动编排基类：派生并实现 before/start/after 三阶段钩子。
"""
from abc import ABC, abstractmethod


class YomkBoot(ABC):
    """Boot hook contract driven by server().boot_ptr(boot).run().

    启动钩子契约，由 server().boot_ptr(boot).run() 驱动。

    Note:
        Hooks run in before -> start -> after order; returning None means success,
        while a non-zero return aborts the remaining hooks and is propagated to run().
        按 before -> start -> after 顺序执行；返回 None 等价成功，
        非零即中止后续钩子并原样返回给 run()。
    """

    def __init__(self) -> None:
        pass

    @abstractmethod
    def before(self) -> int:
        """Run the first boot phase.

        首个启动阶段；返回 0 或 None 继续，非零中止。
        """
        pass

    @abstractmethod
    def start(self) -> int:
        """Run the second boot phase.

        第二阶段；返回 0 或 None 继续，非零中止。
        """
        pass

    @abstractmethod
    def after(self) -> int:
        """Run the final boot phase.

        最后阶段；返回 0 或 None 继续，非零中止。
        """
        pass
