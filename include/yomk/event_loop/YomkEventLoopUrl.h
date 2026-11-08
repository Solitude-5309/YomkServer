#pragma once
#include <string_view>

// /YomkEventLoop/* 内置服务 URL 常量：inline constexpr std::string_view 编译期常量，
// 请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：用户自定义服务的 url 常量可按同风格直接追加。
namespace yomk::url
{
// /YomkEventLoop/*
inline constexpr std::string_view loopStart = "/YomkEventLoop/start";
inline constexpr std::string_view loopStop = "/YomkEventLoop/stop";
inline constexpr std::string_view loopPost = "/YomkEventLoop/post";
inline constexpr std::string_view loopPostWait = "/YomkEventLoop/post_wait";
inline constexpr std::string_view loopDestroy = "/YomkEventLoop/destroy";
inline constexpr std::string_view loopLoops = "/YomkEventLoop/loops";
inline constexpr std::string_view loopLoop = "/YomkEventLoop/loop";
inline constexpr std::string_view loopAll = "/YomkEventLoop/all";
}  // namespace yomk::url