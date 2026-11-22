"""Built-in URL constants and introspection descriptions of the /YomkServerInfo service.

/YomkServerInfo 内置服务 URL 常量与内省描述（格式惯例见 docs/YomkServer.md「内省描述惯例」）。
"""

__all__ = [
    "SRV_ALL", "SRV_FUNCTION", "SRV_FUNCTIONS", "SRV_SERVICES",
    "SRV_ALL_DESC", "SRV_FUNCTION_DESC", "SRV_FUNCTIONS_DESC",
    "SRV_SERVICES_DESC",
]

SRV_SERVICES = "/YomkServerInfo/services"
SRV_FUNCTIONS = "/YomkServerInfo/functions"
SRV_FUNCTION = "/YomkServerInfo/function"
SRV_ALL = "/YomkServerInfo/all"

SRV_SERVICES_DESC = """
# Overview
返回全部已注册服务名清单（含框架内置服务）。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包（字典序），msg "ok"。

# Behavior
同步快照读取，持服务表读锁拷贝排序；幂等。

# Notes
server 已销毁（弱引用失效）→ Failed "server has been destroyed"。
"""
SRV_FUNCTIONS_DESC = """
# Overview
查询指定服务的函数元信息清单。

# Inputs
String 包，value 为服务名（须与安装时的 name() 一致，含前导 "/"）。

# Outputs
data 为 FuncInfoList 包（func_name 为带 "/" 前缀的函数 url 段），msg "ok"。

# Behavior
同步快照读取，先定位服务再取该服务函数表；幂等。

# Notes
Invalid=服务名为空；服务不存在时按现状契约返回 Succeeded + 空表（不归因失败）；server 已销毁 → Failed；msg 非 String → Failed。
"""
SRV_FUNCTION_DESC = """
# Overview
按完整函数 url 查询单函数元信息，不启动服务即可确认某端点期望的消息类型。

# Inputs
String 包，value 为 "/服务名/函数名" 形式的完整 url（须以 / 开头且含第二段）。

# Outputs
data 为 FuncInfo 包（msg_name 恒等于安装时声明的期望消息类型名），msg "ok"。

# Behavior
同步执行：按第一个 / 切出服务名与函数名后精确查找；幂等。

# Notes
url 为空/无 / 前缀/无第二段 → Failed（msg 带具体原因）；服务或函数不存在 → Failed；server 已销毁 → Failed。
"""
SRV_ALL_DESC = """
# Overview
全拓扑快照：服务 → 函数元信息的两层结构。

# Inputs
无（不带请求包）。

# Outputs
data 为 ServerServiceInfoList 包（每项 service_name + funcs），msg "ok"。

# Behavior
同步执行：先取服务名表，再逐服务取函数元信息；幂等。

# Notes
服务名与 funcs 均按字典序；跨服务快照非原子一致；server 已销毁 → Failed。
"""
