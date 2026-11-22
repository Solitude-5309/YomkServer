"""Event loop domain payloads.

事件循环域消息：投递事件、启动配置、单查与内省条目。
"""

from yomk.msgs.YomkMsg import YomkMsg, require_msg_payload


class Event(YomkMsg):
    """Event payload posted to a loop.

    事件投递载荷：目标循环、数据、单次处理器与 tag。
    """

    def __init__(self, event_loop_name="", msg=None, func=None, tag=""):
        require_msg_payload(msg, "Event.msg")
        self.m_eventLoopName = event_loop_name
        self.m_msg = msg
        self.m_serviceFunc = func
        self.m_eventId = 0
        self.m_response = None
        self.m_waitCallback = None
        self.m_tag = tag

    def handle(self):
        if not self.m_serviceFunc:
            return
        self.m_response = self.m_serviceFunc(self.m_msg)


class EventLoopOpts(YomkMsg):
    """Start options payload: loop name, default handler and expected message type.

    启动配置载荷：循环名、默认处理函数与期望消息类型。
    """

    def __init__(self, event_loop_name="", default_service_func=None, msg_name=""):
        self.m_eventLoopName = event_loop_name
        self.m_defaultServiceFunc = default_service_func
        self.m_msgName = msg_name


class EventLoopQuery(YomkMsg):
    """Single loop query payload: name plus head-tag sample count.

    单循环查询载荷：循环名与队头 tag 采样数。
    """

    def __init__(self, loop_name="", tag_count=3):
        self.m_loopName = loop_name   # 字面量精确匹配
        self.m_tagCount = tag_count   # 队头采样条数，缺省 3


class EventLoopInfo(YomkMsg):
    """Loop introspection entry: running state, backlog, default handler and head tags.

    循环内省条目：运行态、积压数、默认函数与队头 tag。
    """

    def __init__(self, loop_name="", running=False, pending=0,
                 has_default_func=False, msg_name="", next_tags=None):
        self.loop_name = loop_name
        self.running = running
        self.pending = pending
        self.has_default_func = has_default_func
        self.msg_name = msg_name
        self.next_tags = next_tags if next_tags is not None else []  # 空串保留原值


class EventLoopInfoList(YomkMsg):
    """List payload of EventLoopInfo.

    EventLoopInfo 清单载荷。
    """

    def __init__(self, items=None):
        self.items = items if items is not None else []
