#pragma once
#include <string_view>

// 内置服务 URL 常量池：inline constexpr std::string_view 编译期常量——零运行时
// 状态、零惰性初始化、零析构顺序问题。请求链（YomkAPI -> YomkServer -> parse ->
// invoke）全程 string_view 透传，服务/函数表以透明比较器异构查找，调用路径零堆分配。
// 访问器名 = 服务前缀 + 路由段驼峰化。
// 采用命名空间而非 struct：与 yomk::payload / yomk::msgs 对齐为协议层三件套；
// namespace 开放可扩展——用户服务可直接在本命名空间内追加同风格常量，
// 亦规避全局符号 YomkUrl 的重名风险
namespace yomk::url
{
// /YomkLogger/*
inline constexpr std::string_view loggerSetConsoleProxy = "/YomkLogger/set_console_log_proxy";
inline constexpr std::string_view loggerSetFileLogProxy = "/YomkLogger/set_file_log_proxy";
inline constexpr std::string_view loggerConsoleLog = "/YomkLogger/console_log";
inline constexpr std::string_view loggerCreateFileLogger = "/YomkLogger/create_file_logger";
inline constexpr std::string_view loggerFileLog = "/YomkLogger/file_log";
inline constexpr std::string_view loggerWriteFileLog = "/YomkLogger/write_file_log";
inline constexpr std::string_view loggerDeleteLogger = "/YomkLogger/delete_logger";
inline constexpr std::string_view loggerOnConsoleLogByLevel = "/YomkLogger/on_console_log_by_level";
inline constexpr std::string_view loggerOffConsoleLogByLevel = "/YomkLogger/off_console_log_by_level";
inline constexpr std::string_view loggerOnFileLogByLevel = "/YomkLogger/on_file_log_by_level";
inline constexpr std::string_view loggerOffFileLogByLevel = "/YomkLogger/off_file_log_by_level";
inline constexpr std::string_view loggerLoggers = "/YomkLogger/loggers";
inline constexpr std::string_view loggerLogger = "/YomkLogger/logger";
inline constexpr std::string_view loggerAll = "/YomkLogger/all";
inline constexpr std::string_view loggerFileGlobalState = "/YomkLogger/file_global_state";
inline constexpr std::string_view loggerConsoleGlobalState = "/YomkLogger/console_global_state";
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
// /YomkEventLoop/*
inline constexpr std::string_view loopStart = "/YomkEventLoop/start";
inline constexpr std::string_view loopStop = "/YomkEventLoop/stop";
inline constexpr std::string_view loopPost = "/YomkEventLoop/post";
inline constexpr std::string_view loopPostWait = "/YomkEventLoop/post_wait";
inline constexpr std::string_view loopDestroy = "/YomkEventLoop/destroy";
inline constexpr std::string_view loopLoops = "/YomkEventLoop/loops";
inline constexpr std::string_view loopLoop = "/YomkEventLoop/loop";
inline constexpr std::string_view loopAll = "/YomkEventLoop/all";
// /YomkFunctionPool/*
inline constexpr std::string_view fpRegister = "/YomkFunctionPool/register";
inline constexpr std::string_view fpUnregister = "/YomkFunctionPool/unregister";
inline constexpr std::string_view fpCall = "/YomkFunctionPool/call";
inline constexpr std::string_view fpNames = "/YomkFunctionPool/names";
inline constexpr std::string_view fpName = "/YomkFunctionPool/name";
inline constexpr std::string_view fpAll = "/YomkFunctionPool/all";
// /YomkServerInfo/*
inline constexpr std::string_view srvServices = "/YomkServerInfo/services";
inline constexpr std::string_view srvFunctions = "/YomkServerInfo/functions";
inline constexpr std::string_view srvFunction = "/YomkServerInfo/function";
inline constexpr std::string_view srvAll = "/YomkServerInfo/all";
}  // namespace yomk::url
