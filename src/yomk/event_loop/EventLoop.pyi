from typing import Any, Callable, Optional

from yomk.msg.event_loop import EventLoopInfo


class Event:
    m_eventLoopName: str
    m_pkg: Any
    m_serviceFunc: Optional[Callable[[Any], Any]]
    m_eventId: int
    m_response: Any
    m_waitCallback: Optional[Callable[[], None]]
    m_tag: str

    def __init__(
        self,
        event_loop_name: str = "",
        pkg: Any = None,
        func: Optional[Callable[[Any], Any]] = None,
        tag: str = ""
    ) -> None: ...
    def handle(self) -> None: ...


class EventLoop:
    # 事件循环底层实例（非服务）：单 worker 线程消费事件队列，
    # YomkEventLoop 按循环名持有（与 Context 模块同款双层写法）
    m_event_queue: Any
    m_queueMutex: Any
    m_condition: Any
    m_thread: Any
    m_running: bool
    m_event_id: int
    m_defaultServiceFunc: Optional[Callable[[Any], Any]]
    m_defaultMsgName: str

    def __init__(self) -> None: ...
    # 双参 msg_name 同存，内省快照依赖
    def set_default_service_func(self, func: Optional[Callable[[Any], Any]],
                                 msg_name: str = "") -> None: ...
    def run(self) -> None: ...
    def start(self) -> int: ...
    def stop(self) -> int: ...
    def destroy(self) -> int: ...
    def __del__(self) -> None: ...
    def post(self, event: Event) -> int: ...
    def post_wait(self, event: Event) -> int: ...
    def info_snapshot(self, info: EventLoopInfo, tag_count: int) -> None: ...