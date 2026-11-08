#pragma once
#include <string_view>

// /YomkFunctionPool/* 内置服务 URL 常量：inline constexpr std::string_view 编译期常量，
// 请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：用户自定义服务的 url 常量可按同风格直接追加。
namespace yomk::url
{
// /YomkFunctionPool/*
inline constexpr std::string_view fpRegister = "/YomkFunctionPool/register";
inline constexpr std::string_view fpUnregister = "/YomkFunctionPool/unregister";
inline constexpr std::string_view fpCall = "/YomkFunctionPool/call";
inline constexpr std::string_view fpNames = "/YomkFunctionPool/names";
inline constexpr std::string_view fpName = "/YomkFunctionPool/name";
inline constexpr std::string_view fpAll = "/YomkFunctionPool/all";
}  // namespace yomk::url