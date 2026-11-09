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
}  // namespace yomk::url