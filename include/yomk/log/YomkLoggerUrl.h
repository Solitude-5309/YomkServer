#pragma once
#include <string_view>

// /YomkLogger/* 内置服务 URL 常量：inline constexpr std::string_view 编译期常量，
// 请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：用户自定义服务的 url 常量可按同风格直接追加。
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
}  // namespace yomk::url