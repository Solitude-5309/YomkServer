#pragma once
#include <string_view>

// /YomkFunctionPool/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkFunctionPool/*
inline constexpr std::string_view fpRegister = "/YomkFunctionPool/register";
inline constexpr std::string_view fpUnregister = "/YomkFunctionPool/unregister";
inline constexpr std::string_view fpCall = "/YomkFunctionPool/call";
inline constexpr std::string_view fpNames = "/YomkFunctionPool/names";
inline constexpr std::string_view fpName = "/YomkFunctionPool/name";
inline constexpr std::string_view fpAll = "/YomkFunctionPool/all";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* fpRegisterDesc = R"(
# Overview
向函数池注册可调用函数：funcName 全池唯一，同名注册即热替换。

# Inputs
Function 包：funcName 函数名；func 处理函数；msgName 期望消息类型名（仅元数据，可空）；description 函数说明（仅元数据，可空）。

# Outputs
data 无负载；首次注册 msg "register function success"，同名热替换 msg "function '<名>' already exists, hot-swap to current function"（status 仍为 Succeeded）。

# Behavior
同步执行，写函数表；注册后即可经 /call 或统一请求通道调用；热替换时 WARN 留痕。

# Notes
热替换为整条覆盖——用两参安装同名函数会清掉既有 msgName/description；Invalid=名/函数为空；pkg 非 Function → Failed。
)";
inline constexpr const char* fpUnregisterDesc = R"(
# Overview
注销函数池中的函数。

# Inputs
String 包，d 为函数名。

# Outputs
data 无负载，msg "unregister function success"。

# Behavior
同步执行，从函数表移除；未注册时 Failed（非幂等）。

# Notes
Invalid=函数名为空；注销后 /call 与 /name 均不再命中；pkg 非 String → Failed。
)";
inline constexpr const char* fpCallDesc = R"(
# Overview
调用函数池中已注册的函数，透传其携带的负载包。

# Inputs
CallFunction 包：funcName 函数名；pkg 传给目标函数的消息包（可空，是否接受由目标函数决定）。

# Outputs
目标函数的 Response 原样回传（status/msg/data 均不加工）。

# Behavior
同步执行：锁内取函数拷贝、锁外调用，避免用户函数持锁；副作用与幂等性取决于目标函数。

# Notes
Invalid=函数名为空；Failed=未注册；目标函数内部的失败不在此端点重新归因。
)";
inline constexpr const char* fpNamesDesc = R"(
# Overview
返回全部已注册函数名清单。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包（map 字典序），msg "ok"。

# Behavior
同步快照读取，持函数表读锁拷贝；幂等。

# Notes
空池返回空表而非 Failed。
)";
inline constexpr const char* fpNameDesc = R"(
# Overview
查询单函数元信息：期望消息类型名与函数说明。

# Inputs
String 包，d 为函数名。

# Outputs
data 为 FuncInfo 包（funcName/msgName/description），msg "ok"。

# Behavior
同步快照读取，命中即构造结构化包；幂等。

# Notes
两参注册的函数 msgName/description 为空串；Invalid=函数名为空；Failed=未注册；pkg 非 String → Failed。
)";
inline constexpr const char* fpAllDesc = R"(
# Overview
全量函数元信息快照。

# Inputs
无（不带请求包）。

# Outputs
data 为 FuncInfoList 包（总数由列表 size 承载，map 字典序），msg "ok"。

# Behavior
同步执行，锁内构造结构化快照；幂等。

# Notes
空池返回空表而非 Failed。
)";
}  // namespace yomk::url
