"""Built-in URL constants and introspection descriptions of the /YomkEventLoop service.

/YomkEventLoop 内置服务 URL 常量与内省描述（格式惯例见 docs/YomkServer.md「内省描述惯例」）。
"""

__all__ = [
    "LOOP_ALL",
    "LOOP_DESTROY",
    "LOOP_LOOP",
    "LOOP_LOOPS",
    "LOOP_POST",
    "LOOP_POST_WAIT",
    "LOOP_START",
    "LOOP_STOP",
    "LOOP_ALL_DESC",
    "LOOP_DESTROY_DESC",
    "LOOP_LOOP_DESC",
    "LOOP_LOOPS_DESC",
    "LOOP_POST_DESC",
    "LOOP_POST_WAIT_DESC",
    "LOOP_START_DESC",
    "LOOP_STOP_DESC",
]
LOOP_START = "/YomkEventLoop/start"
LOOP_STOP = "/YomkEventLoop/stop"
LOOP_POST = "/YomkEventLoop/post"
LOOP_POST_WAIT = "/YomkEventLoop/post_wait"
LOOP_DESTROY = "/YomkEventLoop/destroy"
LOOP_LOOPS = "/YomkEventLoop/loops"
LOOP_LOOP = "/YomkEventLoop/loop"
LOOP_ALL = "/YomkEventLoop/all"

LOOP_START_DESC = """
# Overview
创建事件循环并启动专用消费线程；同名循环已存在则复用既有实例并再次触发启动。

# Inputs
EventLoopOpts 包：m_eventLoopName 循环名；m_defaultServiceFunc 默认处理函数（事件未携带 m_serviceFunc 时使用）；m_msgName 期望消息类型名（仅内省元数据，可空）。

# Outputs
data 无负载，msg "event loop start success"。

# Behavior
同步执行：新名创建 EventLoop、绑定默认处理函数、启动 worker 线程并登记到循环表；同名复用既有实例并 start；已在运行则幂等早退。

# Notes
循环名不做非空校验（空名按空串键登记）；同名复用时本次携带的 m_defaultServiceFunc/m_msgName 不生效；pkg 非 EventLoopOpts → Failed。
"""
LOOP_STOP_DESC = """
# Overview
停止事件循环的消费线程，实例与其事件队列保留。

# Inputs
String 包，value 为循环名。

# Outputs
data 无负载，msg "event loop stop success"。

# Behavior
同步执行并等待 worker join；未运行时直接返回成功（幂等）；队列不清空——未执行事件保留，再次 start 续跑。

# Notes
循环不存在 → Failed（WARN 携循环名）；停机期间 /post 与 /post_wait 均被拒（Failed）；pkg 非 String → Failed。
"""
LOOP_POST_DESC = """
# Overview
向事件循环投递事件：FIFO 入队后立即返回，由消费线程异步执行。

# Inputs
Event 包：m_eventLoopName 目标循环；m_pkg 事件负载；m_serviceFunc 处理函数（None 则用循环默认处理函数）；m_tag 标签（内省采样用）。

# Outputs
data 无负载，msg "event loop post success"。

# Behavior
异步非阻塞：入队即返回；事件由 worker 线程按 FIFO 顺序执行，每次投递各执行一次（非幂等）。

# Notes
循环不存在或未运行 → Failed（未入队）；执行结果不回传，需要结果请用 /post_wait；m_serviceFunc 与默认处理函数均为 None 时事件被执行但无任何效果（静默）；pkg 非 Event → Failed。
"""
LOOP_POST_WAIT_DESC = """
# Overview
投递事件并阻塞调用线程，直到该事件在消费线程执行完毕。

# Inputs
Event 包：m_eventLoopName 目标循环；m_pkg 事件负载；m_serviceFunc 处理函数（None 则用循环默认处理函数）；m_tag 标签。

# Outputs
data 为执行后的 Event 包（m_response 携带处理函数的返回值），msg "event loop post wait success"。

# Behavior
同步阻塞：入队后等待执行完成；在目标 worker 线程内调用会被识别为自锁，改为就地执行该事件并 ERROR 留痕；非幂等。

# Notes
循环不存在或未运行 → Failed（未入队未执行）；循环被 /destroy 丢弃事件时等待者会被唤醒；pkg 非 Event → Failed。
"""
LOOP_DESTROY_DESC = """
# Overview
销毁事件循环：停止 worker、清空未执行队列并释放实例（不可重启）。

# Inputs
String 包，value 为循环名。

# Outputs
data 无负载，msg "event loop destroy success"。

# Behavior
同步执行：先 stop 并 join worker，再锁内清空队列、锁外逐个触发被丢事件的等待回调，以释放 /post_wait 等待者；随后从循环表移除。

# Notes
循环不存在 → Failed（WARN 携循环名）；销毁后同名需重新 /start 创建新实例；pkg 非 String → Failed。
"""
LOOP_LOOPS_DESC = """
# Overview
返回全部事件循环名清单。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包（字典序），msg "ok"。

# Behavior
同步快照读取，持循环表读锁拷贝排序；幂等。

# Notes
stop 后循环仍保留在清单中（仅 /destroy 移除）。
"""
LOOP_LOOP_DESC = """
# Overview
查询单个事件循环的内省快照：运行态、积压数、默认处理函数元信息与队列头 tag 采样。

# Inputs
EventLoopQuery 包：m_loopName 循环名（按字面量精确匹配，不做切分）；m_tagCount 队列头采样条数（缺省 3）。

# Outputs
data 为 EventLoopInfo 包（loop_name/running/pending/has_default_func/msg_name/next_tags），msg "ok"。

# Behavior
同步快照读取，持队列锁拷贝后逐项填充；幂等。

# Notes
m_tagCount 仅接受 int 且非 bool、范围 0..2^64-1，否则回退 3；为 0 时 next_tags 为空；采样存 tag 原值（空串保留）；循环不存在 → Failed（WARN 携循环名）；pkg 非 EventLoopQuery → Failed。
"""
LOOP_ALL_DESC = """
# Overview
全量事件循环内省快照。

# Inputs
无（不带请求包）。

# Outputs
data 为 EventLoopInfoList 包，msg "ok"。

# Behavior
同步执行：锁内拷贝循环引用，锁外逐循环取快照（每个固定采样队列头 3 条 tag）；幂等。

# Notes
采样条数固定 3，需自定义请用 /loop 的 m_tagCount；跨循环快照非原子一致；无循环时返回空表。
"""
