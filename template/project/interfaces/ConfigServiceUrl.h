#pragma once

// /ConfigService/* 工程自有服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/ctx/YomkContextUrl.h），
// URL 为 inline constexpr std::string_view 集中在前，语义描述为 inline constexpr const char* 同名 + Desc 集中在后
// （与 URL 按序一一对应，installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）。
// yomk::url 为开放命名空间：工程自有服务的 url 常量按同风格直接追加（k + 自前缀 Cfg + 端点，如 kCfgLoad）。
namespace yomk::url
{
// /ConfigService/*
inline constexpr std::string_view kCfgLoad = "/ConfigService/load";
inline constexpr std::string_view kCfgGet = "/ConfigService/get";
inline constexpr std::string_view kCfgSet = "/ConfigService/set";
inline constexpr std::string_view kCfgReload = "/ConfigService/reload";
inline constexpr std::string_view kCfgVersion = "/ConfigService/version";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* kCfgLoadDesc = R"(
# Overview
从 Context 指定路径加载配置文件（key: value 文本），填充内存配置表。

# Inputs
无（不带请求包）；配置文件路径取自 Context 键 CTX_CONFIG_PATH。

# Outputs
data 无负载，msg "ok"。

# Behavior
同步执行：读取文件并逐行解析后覆写内存表；空行与无冒号行跳过。

# Notes
Context 无路径键或文件打不开 → Failed；同名键以文件中靠后的值为准。
)";
inline constexpr const char* kCfgGetDesc = R"(
# Overview
按键查询内存配置值。

# Inputs
ConfigKey 包（yomk::msgs::prj::ConfigKey），req.key 为配置键。

# Outputs
data 为 String 包（配置值），msg "ok"。

# Behavior
同步纯读，不修改内存表；幂等。

# Notes
键不存在 → Failed "key not found: <键>"；pkg 非 ConfigKey → Failed。
)";
inline constexpr const char* kCfgSetDesc = R"(
# Overview
写内存配置键值；不落盘。

# Inputs
ConfigKeyValue 包（yomk::msgs::prj::ConfigKeyValue），req.key 与 req.value。

# Outputs
data 无负载，msg "ok"。

# Behavior
同步执行，覆盖写内存表；同值重写结果不变（幂等）；持久化需自行实现 /load 的逆操作。

# Notes
不校验键值合法性；pkg 非 ConfigKeyValue → Failed。
)";
inline constexpr const char* kCfgReloadDesc = R"(
# Overview
清空内存配置表并重新加载配置文件。

# Inputs
无（不带请求包）；路径取自 Context 键 CTX_CONFIG_PATH。

# Outputs
data 无负载，msg "ok"。

# Behavior
同步执行：先清空内存表，再复用 /load 的读取逻辑；副作用同 /load。

# Notes
清空后加载失败 → Failed 且内存表为空（不保留旧值）。
)";
inline constexpr const char* kCfgVersionDesc = R"(
# Overview
返回工程版本号。

# Inputs
无（不带请求包）。

# Outputs
data 为 String 包（版本号，编译期由 CMake project() VERSION 注入 APP_VERSION），msg "ok"。

# Behavior
同步纯读，不修改任何状态；幂等。

# Notes
版本号取自构建配置，运行期不可变。
)";
}  // namespace yomk::url
