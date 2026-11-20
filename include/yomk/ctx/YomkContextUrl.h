#pragma once
#include <string_view>

// /YomkContext/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkContext/*
inline constexpr std::string_view ctxCreate = "/YomkContext/create";
inline constexpr std::string_view ctxGet = "/YomkContext/get";
inline constexpr std::string_view ctxSet = "/YomkContext/set";
inline constexpr std::string_view ctxTurnOnChecker = "/YomkContext/turn_on_checker";
inline constexpr std::string_view ctxTurnOffChecker = "/YomkContext/turn_off_checker";
inline constexpr std::string_view ctxSetChecker = "/YomkContext/set_checker";
inline constexpr std::string_view ctxTurnOnMonitor = "/YomkContext/turn_on_monitor";
inline constexpr std::string_view ctxTurnOffMonitor = "/YomkContext/turn_off_monitor";
inline constexpr std::string_view ctxSetMonitor = "/YomkContext/set_monitor";
inline constexpr std::string_view ctxDestroy = "/YomkContext/destroy";
inline constexpr std::string_view ctxKeys = "/YomkContext/keys";
inline constexpr std::string_view ctxKey = "/YomkContext/key";
inline constexpr std::string_view ctxGroups = "/YomkContext/groups";
inline constexpr std::string_view ctxAll = "/YomkContext/all";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* ctxCreateDesc = R"(
Semantic: 在指定组（缺省 main）创建上下文键并写入值包
Returns: data 无负载，成功 msg "create context success"
Precond: 目标组已存在，键名非空，value 包非空
Idempotent: false —— 键已存在时重调必 Failed
SideEffect: 向组存储写入键值对
Fail: Invalid=键名为空/value 为空；Failed=键已存在
)";
inline constexpr const char* ctxGetDesc = R"(
Semantic: 读取上下文键当前值
Returns: data 为键的值包；键空/键不存在时回填请求携带的默认值并返回默认值包
Precond: 无（未命中也有默认值语义）
Idempotent: true
SideEffect: 无（纯读）
Fail: Invalid=键名为空；Failed=键不存在（两种失败均回默认值）
)";
inline constexpr const char* ctxSetDesc = R"(
Semantic: 更新已存在上下文键的值（值包类型须与原值一致）
Returns: data 无负载，成功 msg "set context success"
Precond: 键已存在；值包类型与原值 typeName 一致
Idempotent: true —— 同值重写结果不变
SideEffect: 覆写组存储；写前经 checker 校验，写后触发 monitor 回调
Fail: Invalid=键空/value 空；Failed=键不存在/类型不匹配/被 checker 拒绝
)";
inline constexpr const char* ctxTurnOnCheckerDesc = "Semantic: 开启指定组 checker 池总闸（组级写前校验生效）；Returns: data 无负载；pkg 非 String Failed";
inline constexpr const char* ctxTurnOffCheckerDesc = "Semantic: 关闭指定组 checker 池总闸（写前校验不再生效）；Returns: data 无负载；pkg 非 String Failed";
inline constexpr const char* ctxSetCheckerDesc = R"(
Semantic: 为已存在键安装写前校验回调（该键每次 set 前调用，拒绝则写入失败）
Returns: data 无负载
Precond: 键已存在，checkFunc 非空
Idempotent: true —— 重复安装为覆盖
SideEffect: 更新键元数据，影响该键后续全部 set
Fail: Invalid=键空/回调空；Failed=键不存在
)";
inline constexpr const char* ctxTurnOnMonitorDesc = "Semantic: 开启指定组 monitor 池总闸（写后监视回调生效）；Returns: data 无负载；pkg 非 String Failed";
inline constexpr const char* ctxTurnOffMonitorDesc = "Semantic: 关闭指定组 monitor 池总闸；Returns: data 无负载；pkg 非 String Failed";
inline constexpr const char* ctxSetMonitorDesc = R"(
Semantic: 为已存在键安装写后监视回调（该键每次 set 成功后触发）
Returns: data 无负载
Precond: 键已存在，monitorFunc 非空
Idempotent: true —— 重复安装为覆盖
SideEffect: 更新键元数据，该键后续每次 set 成功均回调
Fail: Invalid=键空/回调空；Failed=键不存在
)";
inline constexpr const char* ctxDestroyDesc = R"(
Semantic: 删除指定组中的上下文键
Returns: data 无负载，成功 msg "destroy context success"
Precond: 键已存在，键名非空
Idempotent: false —— 键不存在时重调必 Failed
SideEffect: 从组存储移除键值对
Fail: Invalid=键名为空；Failed=键不存在
)";
inline constexpr const char* ctxKeysDesc = "Semantic: 返回指定组全部键名列表（字典序）；Returns: StringArray 包；Failed=组不存在";
inline constexpr const char* ctxKeyDesc = "Semantic: 查询单键内省元信息快照（组名/键名/typeName）；Returns: ContextKeyInfo 包；Invalid=键空；Failed=键不存在";
inline constexpr const char* ctxGroupsDesc = "Semantic: 返回全部组名列表（main 恒首位，自定义组按字典序）；Returns: StringArray 包";
inline constexpr const char* ctxAllDesc = "Semantic: 全部组及其全部键的内省元信息快照；Returns: ContextGroupInfoList 包（组→键元信息层级）";
}  // namespace yomk::url