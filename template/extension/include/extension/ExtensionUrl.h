#pragma once
#include <string_view>

// /ExtensionService/* 扩展服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），
// inline constexpr std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url::ext 为扩展子命名空间：扩展的 url 常量落各自子命名空间，不占 yomk::url 根；
// 常量名一律 k 前缀 + PascalCase：域已由命名空间表达，不再自我前缀（kVersion 而非 kExtVersion）；
// 语义描述常量取同名 + Desc（kVersionDesc）。规范见 docs/yomk命名原则.md §1.4 / §5。
namespace yomk::url::ext
{
// /ExtensionService/*（语义描述与 URL 常量同置：inline constexpr const char*，
//  installFunc 第四参引用，格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr std::string_view kVersion = "/ExtensionService/version";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* kVersionDesc = R"(
# Overview
返回扩展版本号，用于确认当前加载的扩展构建版本。

# Inputs
无（不带请求包）。

# Outputs
data 为 String 包（"extension v<版本号>"，版本号由编译期 EXTENSION_VERSION 注入），msg "ok"。

# Behavior
同步纯读，不修改任何状态；幂等。

# Notes
无失败分支。
)";
}  // namespace yomk::url::ext
