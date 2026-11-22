"""Built-in URL constants and introspection descriptions of the /YomkFunctionPool service.

/YomkFunctionPool 内置服务 URL 常量与内省描述（格式惯例见 docs/YomkServer.md「内省描述惯例」）。
"""

__all__ = [
    "FP_ALL",
    "FP_CALL",
    "FP_NAME",
    "FP_NAMES",
    "FP_REGISTER",
    "FP_UNREGISTER",
    "FP_ALL_DESC",
    "FP_CALL_DESC",
    "FP_NAME_DESC",
    "FP_NAMES_DESC",
    "FP_REGISTER_DESC",
    "FP_UNREGISTER_DESC",
]

FP_REGISTER = "/YomkFunctionPool/register"
FP_UNREGISTER = "/YomkFunctionPool/unregister"
FP_CALL = "/YomkFunctionPool/call"
FP_NAMES = "/YomkFunctionPool/names"
FP_NAME = "/YomkFunctionPool/name"
FP_ALL = "/YomkFunctionPool/all"

FP_REGISTER_DESC = """
# Overview
向函数池注册可调用函数：name 全池唯一，同名注册即热替换。

# Inputs
Function 包：name 函数名；func 处理函数；msg_name 期望消息类型名（仅元数据，可空）；description 函数说明（仅元数据，可空）。

# Outputs
data 无负载；首次注册 msg "register function success"，同名热替换 msg "function '<名>' already exists, hot-swap to current function"（status 仍为 Succeeded）。

# Behavior
同步执行，写函数表；注册后即可经 /call 或统一请求通道调用；热替换时 WARN 留痕。

# Notes
热替换为整条覆盖——用缺省 msg_name/description 重装同名函数会清掉既有元数据；Invalid=name/func 为空；msg 非 Function → Failed。
"""
FP_UNREGISTER_DESC = """
# Overview
注销函数池中的函数。

# Inputs
String 包，value 为函数名。

# Outputs
data 无负载，msg "unregister function success"。

# Behavior
同步执行，从函数表移除；未注册时 Failed（非幂等）。

# Notes
Invalid=函数名为空；注销后 /call 与 /name 均不再命中；msg 非 String → Failed。
"""
FP_CALL_DESC = """
# Overview
调用函数池中已注册的函数，透传其携带的负载包。

# Inputs
CallFunction 包：name 函数名；msg 传给目标函数的消息包（可空，是否接受由目标函数决定）。

# Outputs
目标函数的返回值原样回传（不做任何加工；框架约定目标函数返回 Response）。

# Behavior
同步执行：锁内取函数引用、锁外调用，避免用户函数持锁；副作用与幂等性取决于目标函数。

# Notes
Invalid=函数名为空；Failed=未注册；目标函数内部的失败不在此端点重新归因。
"""
FP_NAMES_DESC = """
# Overview
返回全部已注册函数名清单。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包（字典序），msg "ok"。

# Behavior
同步快照读取，持函数表读锁拷贝排序；幂等。

# Notes
空池返回空表而非 Failed。
"""
FP_NAME_DESC = """
# Overview
查询单函数元信息：期望消息类型名与函数说明。

# Inputs
String 包，value 为函数名。

# Outputs
data 为 FuncInfo 包（func_name/msg_name/description），msg "ok"。

# Behavior
同步快照读取，命中即构造结构化包；幂等。

# Notes
未声明 msg_name/description 的函数两字段为空串；Invalid=函数名为空；Failed=未注册；msg 非 String → Failed。
"""
FP_ALL_DESC = """
# Overview
全量函数元信息快照。

# Inputs
无（不带请求包）。

# Outputs
data 为 FuncInfoList 包（总数由列表长度承载，字典序），msg "ok"。

# Behavior
同步执行，锁内构造结构化快照；幂等。

# Notes
空池返回空表而非 Failed。
"""
