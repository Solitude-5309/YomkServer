#pragma once
#include <string_view>

// /ExtensionService/* 扩展服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），
// inline constexpr std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url::ext 为扩展子命名空间：扩展的 url 常量落各自子命名空间，不占 yomk::url 根；
// 域已由命名空间表达，常量名不再自我前缀（version 而非 extVersion）。
namespace yomk::url::ext
{
// /ExtensionService/*（语义描述与 URL 常量同置：inline constexpr const char*，
//  installFunc 第四参引用，格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr std::string_view version = "/ExtensionService/version";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* versionDesc = R"(
Semantic: 返回扩展版本号
Returns: data 为 String 包（"extension v<版本号>"）
Idempotent: true
SideEffect: 无（纯读）
)";
}  // namespace yomk::url::ext
