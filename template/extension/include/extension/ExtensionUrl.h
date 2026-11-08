#pragma once
#include <string_view>

// /ExtensionService/* 扩展服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），
// inline constexpr std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：扩展的 url 常量按同风格直接追加。
namespace yomk::url
{
// /ExtensionService/*
inline constexpr std::string_view extVersion = "/ExtensionService/version";
}  // namespace yomk::url
