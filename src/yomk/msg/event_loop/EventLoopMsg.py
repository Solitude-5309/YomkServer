class EventLoopPkg:
    def __init__(self, event_loop_name="", default_service_func=None, msg_name=""):
        self.m_eventloopName = event_loop_name
        self.m_defaultServiceFunc = default_service_func
        self.m_msgName = msg_name


class EventLoopQuery:
    # 事件循环单查载荷（/YomkEventLoop/loop 专用）：
    # 名称 + 队列头 tag 采样数，类型化字段无解析歧义
    def __init__(self, loop_name="", tag_count=3):
        self.m_loopName = loop_name   # 循环名（按字面量精确匹配，不做任何切分）
        self.m_tagCount = tag_count   # 队列头采样条数（缺省 3，与全量内省缺省一致）


class EventLoopInfo:
    # 事件循环内省元信息（/YomkEventLoop/loop 与 /YomkEventLoop/all 返回数据）
    def __init__(self, loop_name="", running=False, pending=0,
                 has_default_func=False, msg_name="", next_tags=None):
        self.loop_name = loop_name        # 循环名
        self.running = running            # worker 是否运行中
        self.pending = pending            # 积压事件数
        self.has_default_func = has_default_func  # 是否注册默认处理函数
        self.msg_name = msg_name          # 默认处理函数期望消息类型名（可空）
        # 队列头起最多 tag_count 条事件 tag（存原值，空串保留）
        self.next_tags = next_tags if next_tags is not None else []