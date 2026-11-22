from typing import Any, Callable, List, Optional
from yomk.msgs.YomkMsg import YomkMsg


class Event(YomkMsg):
    m_eventLoopName: str
    m_msg: Optional[YomkMsg]
    m_serviceFunc: Optional[Callable[[Any], Any]]
    m_eventId: int
    m_response: Any
    m_waitCallback: Any
    m_tag: str

    def __init__(
        self,
        event_loop_name: str = "",
        msg: Optional[YomkMsg] = None,
        func: Optional[Callable[[Any], Any]] = None,
        tag: str = ""
    ) -> None: ...

    def handle(self) -> None: ...


class EventLoopOpts(YomkMsg):
    m_eventLoopName: str
    m_defaultServiceFunc: Optional[Callable[[Any], Any]]
    m_msgName: str

    def __init__(
        self,
        event_loop_name: str = "",
        default_service_func: Optional[Callable[[Any], Any]] = None,
        msg_name: str = ""
    ) -> None: ...


class EventLoopQuery(YomkMsg):
    m_loopName: str
    m_tagCount: int

    def __init__(self, loop_name: str = "", tag_count: int = 3) -> None: ...


class EventLoopInfo(YomkMsg):
    loop_name: str
    running: bool
    pending: int
    has_default_func: bool
    msg_name: str
    next_tags: List[str]

    def __init__(self, loop_name: str = "", running: bool = False,
                 pending: int = 0, has_default_func: bool = False,
                 msg_name: str = "",
                 next_tags: Optional[List[str]] = None) -> None: ...


class EventLoopInfoList(YomkMsg):
    items: List[EventLoopInfo]

    def __init__(self, items: Optional[List[EventLoopInfo]] = None) -> None: ...
