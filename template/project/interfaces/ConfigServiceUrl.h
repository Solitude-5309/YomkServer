#pragma once

// /ConfigService/* 工程自有服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/ctx/YomkContextUrl.h），
// URL 为 inline constexpr std::string_view，语义描述为紧跟其后的 inline constexpr const char* <常量名>Desc
// （installFunc 第四参引用，格式惯例见 docs/YomkServer.md「内省描述惯例」）。
// yomk::url 为开放命名空间：工程自有服务的 url 常量按同风格直接追加（自前缀 cfg 区分模块）。
namespace yomk::url
{
// /ConfigService/*
inline constexpr std::string_view cfgLoad = "/ConfigService/load";
inline constexpr const char* cfgLoadDesc = R"(
Semantic: 从 Context 指定路径加载配置文件（key: value 文本），填充内存配置表
Returns: data 无负载
Precond: Context 中已存在配置文件路径键且文件可读
Idempotent: true —— 重复加载以文件内容覆盖内存表
SideEffect: 覆写内存配置表
Fail: Failed=Context 无路径键/文件打开失败（空行与无冒号行跳过）
)";
inline constexpr std::string_view cfgGet = "/ConfigService/get";
inline constexpr const char* cfgGetDesc = "Semantic: 按键查询内存配置值；Returns: data 为 String 包；Failed=pkg 非 ConfigKey/键不存在";
inline constexpr std::string_view cfgSet = "/ConfigService/set";
inline constexpr const char* cfgSetDesc = "Semantic: 写内存配置键值（不落盘，持久化需扩展 /load 逆操作）；Failed=pkg 非 ConfigKeyValue";
inline constexpr std::string_view cfgReload = "/ConfigService/reload";
inline constexpr const char* cfgReloadDesc = "Semantic: 清空内存配置表并重新加载配置文件；Returns: data 无负载；Failed=同 /load";
inline constexpr std::string_view cfgVersion = "/ConfigService/version";
inline constexpr const char* cfgVersionDesc = "Semantic: 返回工程版本号（编译期 APP_VERSION 注入）；Returns: data 为 String 包";
}  // namespace yomk::url
