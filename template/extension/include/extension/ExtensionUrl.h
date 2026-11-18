#pragma once
#include <string_view>

// /ExtensionService/* 扩展服务 URL 常量：与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），
// inline constexpr std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url::ext 为扩展子命名空间：扩展的 url 常量落各自子命名空间，不占 yomk::url 根；
// 域已由命名空间表达，常量名不再自我前缀（version 而非 extVersion）。
namespace yomk::url::ext
{
// /ExtensionService/*
inline constexpr std::string_view version = "/ExtensionService/version";
}  // namespace yomk::url::ext
