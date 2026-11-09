#pragma once
#include <string_view>

// /YomkServerInfo/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkServerInfo/*
inline constexpr std::string_view srvServices = "/YomkServerInfo/services";
inline constexpr std::string_view srvFunctions = "/YomkServerInfo/functions";
inline constexpr std::string_view srvFunction = "/YomkServerInfo/function";
inline constexpr std::string_view srvAll = "/YomkServerInfo/all";
}  // namespace yomk::url