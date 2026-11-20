#pragma once
#include <string_view>

// /YomkFunctionPool/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkFunctionPool/*
inline constexpr std::string_view fpRegister = "/YomkFunctionPool/register";
inline constexpr const char* fpRegisterDesc = R"(
Semantic: 向函数池注册可调用函数（funcName 全池唯一）
Returns: data 无负载，成功 msg "register function success"
Precond: funcName 非空，func 非空
Idempotent: true —— 同名重复注册为热替换覆盖（WARN 留痕）
SideEffect: 写函数表；该函数此后可经 /call 与统一请求通道执行
Fail: Invalid=funcName/func 为空；Failed=pkg 非 Function
)";
inline constexpr std::string_view fpUnregister = "/YomkFunctionPool/unregister";
inline constexpr const char* fpUnregisterDesc = "Semantic: 注销函数池中的函数；Returns: data 无负载；Invalid=funcName 空；Failed=未注册";
inline constexpr std::string_view fpCall = "/YomkFunctionPool/call";
inline constexpr const char* fpCallDesc = R"(
Semantic: 调用函数池中已注册函数，透传其携带的负载包
Returns: 由目标函数决定（目标函数响应原样回传）
Precond: funcName 已注册
Idempotent: 取决于目标函数
SideEffect: 执行目标函数（副作用随目标函数）
Fail: Invalid=funcName 空；Failed=未注册
)";
inline constexpr std::string_view fpNames = "/YomkFunctionPool/names";
inline constexpr const char* fpNamesDesc = "Semantic: 返回全部注册函数名列表（字典序）；Returns: StringArray 包";
inline constexpr std::string_view fpName = "/YomkFunctionPool/name";
inline constexpr const char* fpNameDesc = "Semantic: 查询单函数元信息（期望消息类型+函数说明）；Returns: FuncInfo 包（m_msg 恒等于声明 msgName 的特殊契约）；Invalid=funcName 空；Failed=未注册";
inline constexpr std::string_view fpAll = "/YomkFunctionPool/all";
inline constexpr const char* fpAllDesc = "Semantic: 全量函数元信息快照；Returns: FuncInfoList 包（总数由 d.size() 承载）";
}  // namespace yomk::url