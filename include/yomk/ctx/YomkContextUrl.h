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
)";
inline constexpr const char* ctxGetDesc = R"(
# Overview
读取上下文键当前值；键名为空或键不存在时回填并返回请求携带的默认值，故"未命中"不等于无返回。

# Inputs
Context 包：key 键名；value 默认值包（可为空）；group 组名（缺省 "main"）。

# Outputs
data 为键的当前值包；未命中时 data 为请求携带的默认值包（默认值可为 nullptr）。

# Behavior
同步纯读，不修改组存储；幂等，任意次调用结果一致。

# Notes
key 为空 → Invalid（仍回默认值）；键不存在 → Failed（仍回默认值）；pkg 非 Context → Failed；调用方须按 status 区分"取到默认值"与"取到现值"。
)";
inline constexpr const char* ctxSetDesc = R"(
# Overview
更新已存在上下文键的值，值包类型必须与原值类型一致；本端点不创建键。

# Inputs
Context 包：key 键名；value 新值包（非空）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set context success"；失败时 msg 携带期望/实际类型名或拒绝来源。

# Behavior
同步执行：组级 checker 总闸开启且该键已装 checker 时先校验，再整体替换值对象，随后触发 monitor（同步形态在调用线程、异步形态在组监控池线程）。

# Notes
同值重写结果不变（幂等）；Invalid=key/value 为空；Failed=键不存在/类型不匹配/被 checker 拒绝；同步 monitor 内重入 set 会递归。
)";
inline constexpr const char* ctxTurnOnCheckerDesc = R"(
# Overview
开启指定组的 checker 总闸，使该组已安装的键级写前校验回调参与 set。

# Inputs
String 包，d 为组名（缺省组请显式传 "main"）。

# Outputs
data 无负载，成功 msg "turn on checker success"。

# Behavior
同步执行，组级 atomic 开关置 true；幂等，重复开启无额外效果。

# Notes
新建组的 checker 总闸初始为关闭；未装 checker 的键在总闸开启后仍视为放行；pkg 非 String → Failed。
)";
inline constexpr const char* ctxTurnOffCheckerDesc = R"(
# Overview
关闭指定组的 checker 总闸，该组已安装的键级校验回调不再被调用。

# Inputs
String 包，d 为组名。

# Outputs
data 无负载，成功 msg "turn off checker success"。

# Behavior
同步执行，组级 atomic 开关置 false；幂等。

# Notes
仅停用校验，已安装的回调保留——重新开启总闸即恢复；pkg 非 String → Failed。
)";
inline constexpr const char* ctxSetCheckerDesc = R"(
# Overview
为已存在的键安装写前校验回调；该键每次 set 前调用，返回 Reject 则写入失败。

# Inputs
ContextChecker 包：key 键名；checkFunc 校验回调（非空，入参为待写入的 Context 快照）；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set checker success"。

# Behavior
同步执行，更新键元数据；重复安装为覆盖（幂等）。

# Notes
回调仅在组 checker 总闸开启时生效；Invalid=key/checkFunc 为空；Failed=键不存在；pkg 非 ContextChecker → Failed。
)";
inline constexpr const char* ctxTurnOnMonitorDesc = R"(
# Overview
开启指定组的 monitor 总闸，使该组已安装的写后监视回调在 set 成功后触发。

# Inputs
String 包，d 为组名。

# Outputs
data 无负载，成功 msg "turn on monitor success"。

# Behavior
同步执行，组级 atomic 开关置 true；幂等。

# Notes
新建组的 monitor 总闸初始为关闭；关闭期间 set 仍成功，仅跳过回调；pkg 非 String → Failed。
)";
inline constexpr const char* ctxTurnOffMonitorDesc = R"(
# Overview
关闭指定组的 monitor 总闸，该组已安装的写后监视回调不再触发。

# Inputs
String 包，d 为组名。

# Outputs
data 无负载，成功 msg "turn off monitor success"。

# Behavior
同步执行，组级 atomic 开关置 false；幂等。

# Notes
仅停用通知，已安装的回调保留——重新开启总闸即恢复；pkg 非 String → Failed。
)";
inline constexpr const char* ctxSetMonitorDesc = R"(
# Overview
为已存在的键安装写后监视回调；该键每次 set 成功后触发。

# Inputs
ContextMonitor 包：key 键名；contextMonitorFunc 回调（非空）；asyncMonitor 是否异步；group 组名（缺省 "main"）。

# Outputs
data 无负载，成功 msg "set context monitor success"。

# Behavior
同步执行，更新键元数据；重复安装为覆盖（幂等）；异步形态投递到组监控池（单线程 FIFO，按 set 提交序送达），同步形态在 set 调用线程内执行。

# Notes
回调仅在组 monitor 总闸开启时触发；回调收到本次 set 的键值快照，不保证实时性；Invalid=key/回调为空；Failed=键不存在；pkg 非 ContextMonitor → Failed。
)";
inline constexpr const char* ctxDestroyDesc = R"(
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
)";
inline constexpr const char* ctxKeysDesc = R"(
# Overview
返回指定组全部键名清单（map 字典序）。

# Inputs
String 包，d 为组名。

# Outputs
data 为 StringArray 包，msg "ok"。

# Behavior
同步快照读取，持组读锁拷贝键名；幂等。

# Notes
组不存在时按名惰性创建并返回空表（不归因失败）；pkg 非 String → Failed。
)";
inline constexpr const char* ctxKeyDesc = R"(
# Overview
查询单键内省元信息快照：键名、值消息包类型名、checker 安装态、monitor 数量与所属组。

# Inputs
ContextGroupKey 包：group 组名；key 键名。

# Outputs
data 为 ContextKeyInfo 包（key/typeName/checkerOn/monitorCount/asyncMonitorCount/group），msg "ok"。

# Behavior
同步快照读取，持组读锁构造；幂等。

# Notes
checkerOn 报该键是否已装 checker（非组总闸态）；typeName 为值消息包类型名；Invalid=键名为空；Failed=键不存在；pkg 非 ContextGroupKey → Failed。
)";
inline constexpr const char* ctxGroupsDesc = R"(
# Overview
返回全部组名清单（main 恒首位，自定义组按 map 字典序追加）。

# Inputs
无（不带请求包）。

# Outputs
data 为 StringArray 包，msg "ok"。

# Behavior
同步快照读取，持组表读锁拷贝；幂等。

# Notes
组清单只增不减——自定义组一经创建不会消失。
)";
inline constexpr const char* ctxAllDesc = R"(
# Overview
返回全拓扑快照：全部组及其全部键的内省元信息（组 → 键两层结构）。

# Inputs
无（不带请求包）。

# Outputs
data 为 ContextGroupInfoList 包（每项 groupName + keys），msg "ok"。

# Behavior
同步执行：先取 main 组快照，再锁内拷贝自定义组指针、锁外逐组取快照；幂等。

# Notes
main 组恒首位；keys 按 map 字典序；跨组快照非原子一致（各组分别加锁）。
)";
}  // namespace yomk::url
