from typing import Any, Callable, List, Optional


class EventLoopPkg:
    # 字段名与 C++ payload m_eventLoopName（大写 L）一致，裸链载荷跨语言可移植
    m_eventLoopName: str
    m_defaultServiceFunc: Optional[Callable[[Any], Any]]
    m_msgName: str

    def __init__(
        self,
        event_loop_name: str = "",
        default_service_func: Optional[Callable[[Any], Any]] = None,
        msg_name: str = ""
    ) -> None: ...


class EventLoopQuery:
    # 事件循环单查载荷（/YomkEventLoop/loop 专用）：
    # 名称 + 队列头 tag 采样数，类型化字段无解析歧义
    m_loopName: str  # 循环名（按字面量精确匹配，不做任何切分）
    m_tagCount: int  # 队列头采样条数（缺省 3，与全量内省缺省一致）

    def __init__(self, loop_name: str = "", tag_count: int = 3) -> None: ...


class EventLoopInfo:
    # 事件循环内省元信息（/YomkEventLoop/loop 与 /YomkEventLoop/all 返回数据）
    loop_name: str                      # 循环名
    running: bool                       # worker 是否运行中
    pending: int                        # 积压事件数
    has_default_func: bool              # 是否注册默认处理函数
    msg_name: str                       # 默认处理函数期望消息类型名（可空）
    next_tags: List[str]                # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）

    def __init__(self, loop_name: str = "", running: bool = False,
                 pending: int = 0, has_default_func: bool = False,
                 msg_name: str = "",
                 next_tags: Optional[List[str]] = None) -> None: ...