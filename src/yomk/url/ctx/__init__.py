"""Built-in URL constants and introspection descriptions of the /YomkContext service.

/YomkContext 内置服务 URL 常量与内省描述（格式惯例见 docs/YomkServer.md「内省描述惯例」）。
"""

__all__ = [
    "CTX_ALL",
    "CTX_CREATE",
    "CTX_DESTROY",
    "CTX_GET",
    "CTX_GROUPS",
    "CTX_KEY",
    "CTX_KEYS",
    "CTX_SET",
    "CTX_SET_CHECKER",
    "CTX_SET_MONITOR",
    "CTX_TURN_OFF_CHECKER",
    "CTX_TURN_OFF_MONITOR",
    "CTX_TURN_ON_CHECKER",
    "CTX_TURN_ON_MONITOR",
    "CTX_ALL_DESC",
    "CTX_CREATE_DESC",
    "CTX_DESTROY_DESC",
    "CTX_GET_DESC",
    "CTX_GROUPS_DESC",
    "CTX_KEY_DESC",
    "CTX_KEYS_DESC",
    "CTX_SET_DESC",
    "CTX_SET_CHECKER_DESC",
    "CTX_SET_MONITOR_DESC",
    "CTX_TURN_OFF_CHECKER_DESC",
    "CTX_TURN_OFF_MONITOR_DESC",
    "CTX_TURN_ON_CHECKER_DESC",
    "CTX_TURN_ON_MONITOR_DESC",
]
CTX_CREATE = "/YomkContext/create"
CTX_GET = "/YomkContext/get"
CTX_SET = "/YomkContext/set"
CTX_TURN_ON_CHECKER = "/YomkContext/turn_on_checker"
CTX_TURN_OFF_CHECKER = "/YomkContext/turn_off_checker"
CTX_SET_CHECKER = "/YomkContext/set_checker"
CTX_TURN_ON_MONITOR = "/YomkContext/turn_on_monitor"
CTX_TURN_OFF_MONITOR = "/YomkContext/turn_off_monitor"
CTX_SET_MONITOR = "/YomkContext/set_monitor"
CTX_DESTROY = "/YomkContext/destroy"
CTX_KEYS = "/YomkContext/keys"
CTX_KEY = "/YomkContext/key"
CTX_GROUPS = "/YomkContext/groups"
CTX_ALL = "/YomkContext/all"

CTX_CREATE_DESC = """
# Overview
在指定组（缺省 main）创建上下文键并写入值包；create 是键的唯一创建入口，set 只能更新已存在的键。

# Inputs
Context 包：key 键名；value 值包（非空）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "create context success"。

# Behavior
同步执行，在组存储写入键值对；非幂等——键已存在时重调必 Failed。

# Notes
key/value 为空 → Invalid；group 名为空 → Invalid；自定义组不存在时按名惰性创建；pkg 非 Context → Failed。
"""
CTX_GET_DESC = """
# Overview
读取上下文键当前值；键名为空或键不存在时回填并返回请求携带的默认值，故"未命中"不等于无返回。

# Inputs
Context 包：key 键名；value 默认值包（可为 None）；group 组名（缺省 "main"）。

# Outputs
data 为键的当前值包；未命中时 data 为请求携带的默认值包（可为 None）。

# Behavior
同步纯读，不修改组存储；幂等，任意次调用结果一致。

# Notes
key 为空 → Invalid（仍回默认值）；键不存在 → Failed（仍回默认值）；group 名为空 → Invalid；pkg 非 Context → Failed；调用方须按 status 区分"取到默认值"与"取到现值"。
"""
CTX_SET_DESC = """
# Overview
更新已存在上下文键的值，值包类型必须与原值类型一致；本端点不创建键。

# Inputs
Context 包：key 键名；value 新值包（非 None）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set context success"；失败时 msg 携带期望/实际类型名或拒绝来源。

# Behavior
同步执行：组级 checker 总闸开启且该键已装 checker 时先校验，再整体替换值对象，随后触发 monitor（同步形态在调用线程、异步形态在组监控池线程）。

# Notes
同值重写结果不变（幂等）；Invalid=key/value 为空；Failed=键不存在/类型不匹配/被 checker 拒绝；同步 monitor 内重入 set 会递归。
"""
CTX_TURN_ON_CHECKER_DESC = """
# Overview
开启指定组的 checker 总闸，使该组已安装的键级写前校验回调参与 set。

# Inputs
String 包，value 为组名（缺省组请显式传 "main"）。

# Outputs
data 无负载，成功 msg "turn on checker success"。

# Behavior
同步执行，组级开关置 True；幂等，重复开启无额外效果。

# Notes
新建组的 checker 总闸初始为关闭；未装 checker 的键在总闸开启后仍视为放行；group 名为空 → Invalid；pkg 非 String → Failed。
"""
CTX_TURN_OFF_CHECKER_DESC = """
# Overview
关闭指定组的 checker 总闸，该组已安装的键级校验回调不再被调用。

# Inputs
String 包，value 为组名。

# Outputs
data 无负载，成功 msg "turn off checker success"。

# Behavior
同步执行，组级开关置 False；幂等。

# Notes
仅停用校验，已安装的回调保留——重新开启总闸即恢复；group 名为空 → Invalid；pkg 非 String → Failed。
"""
CTX_SET_CHECKER_DESC = """
# Overview
为已存在的键安装写前校验回调；该键每次 set 前调用，返回 CheckStatus.eReject 则写入失败。

# Inputs
ContextChecker 包：key 键名；check_func 校验回调（非空，入参为待写入的 Context 快照）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set checker success"。

# Behavior
同步执行，更新键元数据；每键单回调，重复安装为覆盖（幂等）。

# Notes
回调仅在组 checker 总闸开启时生效；Invalid=key/check_func 为空；Failed=键不存在；pkg 非 ContextChecker → Failed。
"""
CTX_TURN_ON_MONITOR_DESC = """
# Overview
开启指定组的 monitor 总闸，使该组已安装的写后监视回调在 set 成功后触发。

# Inputs
String 包，value 为组名。

# Outputs
data 无负载，成功 msg "turn on monitor success"。

# Behavior
同步执行，组级开关置 True；幂等。

# Notes
新建组的 monitor 总闸初始为关闭；关闭期间 set 仍成功，仅跳过回调；group 名为空 → Invalid；pkg 非 String → Failed。
"""
CTX_TURN_OFF_MONITOR_DESC = """
# Overview
关闭指定组的 monitor 总闸，该组已安装的写后监视回调不再触发。

# Inputs
String 包，value 为组名。

# Outputs
data 无负载，成功 msg "turn off monitor success"。

# Behavior
同步执行，组级开关置 False；幂等。

# Notes
仅停用通知，已安装的回调保留——重新开启总闸即恢复；group 名为空 → Invalid；pkg 非 String → Failed。
"""
CTX_SET_MONITOR_DESC = """
# Overview
为已存在的键追加写后监视回调；该键每次 set 成功后逐个触发。

# Inputs
ContextMonitor 包：key 键名；monitor_func 回调（非空）；async_monitor 是否异步投递；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set context monitor success"。

# Behavior
同步执行，向键的监听列表追加一条；异步形态投递到组监控池（单线程 FIFO，按 set 提交序送达），同步形态在 set 调用线程内执行。

# Notes
同一键可挂多个监听（重复安装为追加，非覆盖）；回调仅在组 monitor 总闸开启时触发，收到本次 set 的键值快照；Invalid=key/monitor_func 为空；Failed=键不存在；pkg 非 ContextMonitor → Failed。
"""
CTX_DESTROY_DESC = """
# Overview
删除指定组中的上下文键，连同其值、checker 与 monitor 元数据。

# Inputs
ContextGroupKey 包：group 组名（缺省 "main"）；key 键名。

# Outputs
data 无负载，成功 msg "destroy context success"。

# Behavior
同步执行，从组存储移除键；非幂等——键不存在时重调必 Failed。

# Notes
自定义组本身不会被销毁（组表只增不减）；Invalid=键名为空；Failed=键不存在；pkg 非 ContextGroupKey → Failed。
"""
CTX_KEYS_DESC = """
# Overview
返回指定组全部键名清单（字典序）。

# Inputs
String 包，value 为组名。

# Outputs
data 为 StringArray 包，msg "ok"。

# Behavior
同步快照读取，持组读锁拷贝键名；幂等。

# Notes
组不存在时按名惰性创建并返回空表（不归因失败）；group 名为空 → Invalid；pkg 非 String → Failed。
"""
CTX_KEY_DESC = """
# Overview
查询单键内省元信息快照：键名、值消息包类型名、checker 安装态、monitor 数量与所属组。

# Inputs
ContextGroupKey 包：group 组名；key 键名。

# Outputs
data 为 ContextKeyInfo 包（key/type_name/checker_on/monitor_count/async_monitor_count/group），msg "ok"。

# Behavior
同步快照读取，持组读锁构造；幂等。

# Notes
checker_on 报该键是否已装 checker（非组总闸态）；type_name 为值消息包类型名（值为 None 时为空串）；Invalid=键名为空；Failed=键不存在；pkg 非 ContextGroupKey → Failed。
"""
CTX_GROUPS_DESC = """
# Overview
返回全部组名清单（main 恒首位，自定义组按字典序追加）。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包，msg "ok"。

# Behavior
同步快照读取，持组表读锁拷贝；幂等。

# Notes
组清单只增不减——自定义组一经创建不会消失。
"""
CTX_ALL_DESC = """
# Overview
返回全拓扑快照：全部组及其全部键的内省元信息（组 → 键两层结构）。

# Inputs
无（不带请求包）。

# Outputs
data 为 ContextGroupInfoList 包（每项 group_name + keys），msg "ok"。

# Behavior
同步执行：先取 main 组快照，再锁内拷贝自定义组指针、锁外逐组取快照；幂等。

# Notes
main 组恒首位；keys 按字典序；跨组快照非原子一致（各组分别加锁）。
"""
