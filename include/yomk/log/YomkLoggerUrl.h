#pragma once
#include <string_view>

// /YomkLogger/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
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

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* loggerSetConsoleProxyDesc = R"(
Semantic: 安装控制台输出代理，拦截全部控制台日志（回调返回 true 短路默认输出）
Returns: data 无负载
Precond: 回调函数非空
Idempotent: true —— 重复安装为覆盖
SideEffect: 替换全局控制台代理回调，日志是否落 stdout 由此改变
Fail: Failed=pkg 非 ConsoleLogProxy
)";
inline constexpr const char* loggerSetFileLogProxyDesc = R"(
Semantic: 安装文件输出代理，拦截全部文件日志（回调返回 true 短路默认落盘）
Returns: data 无负载
Precond: 回调函数非空
Idempotent: true —— 重复安装为覆盖
SideEffect: 替换全局文件代理回调，日志是否落文件由此改变
Fail: Failed=pkg 非 FileLogProxy
)";
inline constexpr const char* loggerConsoleLogDesc = "Semantic: 写控制台日志（代理短路优先，否则按级别门控输出）；Returns: data 无负载；Failed=pkg 非 Log";
inline constexpr const char* loggerCreateFileLoggerDesc = R"(
Semantic: 创建文件日志器并初始化落盘文件（四级日志先入缓冲，WRITE 落盘）
Returns: data 无负载
Precond: 日志器名与目录非空
Idempotent: false —— 重名创建必 Failed
SideEffect: 创建目录与文件、注册日志器
Fail: Invalid=名/目录空；Failed=重名/初始化失败
)";
inline constexpr const char* loggerFileLogDesc = "Semantic: 向指定文件日志器缓冲写入一条日志（WRITE 前不落盘）；Returns: data 无负载；Failed=日志器不存在";
inline constexpr const char* loggerWriteFileLogDesc = R"(
Semantic: 将指定文件日志器的缓冲内容落盘并清空缓冲
Returns: data 无负载
Precond: 日志器已创建
Idempotent: true —— 空缓冲重复 WRITE 无副作用
SideEffect: 追加写文件、清空内存缓冲
Fail: Failed=日志器不存在
)";
inline constexpr const char* loggerDeleteLoggerDesc = "Semantic: 删除指定文件日志器；Returns: data 无负载；Failed=日志器不存在";
inline constexpr const char* loggerOnConsoleLogByLevelDesc = "Semantic: 恢复 Log 包指定级别的控制台输出门控；Returns: data 无负载；未知级别忽略";
inline constexpr const char* loggerOffConsoleLogByLevelDesc = "Semantic: 关闭 Log 包指定级别的控制台输出门控；Returns: data 无负载；未知级别忽略";
inline constexpr const char* loggerOnFileLogByLevelDesc = "Semantic: 恢复 Log 包指定级别的文件输出门控；Returns: data 无负载；未知级别忽略";
inline constexpr const char* loggerOffFileLogByLevelDesc = "Semantic: 关闭 Log 包指定级别的文件输出门控；Returns: data 无负载；未知级别忽略";
inline constexpr const char* loggerLoggersDesc = "Semantic: 查询日志器名列表（入参 \"console\" 或 \"file\" 选表，字典序）；Returns: StringArray 包；Failed=类型未知";
inline constexpr const char* loggerLoggerDesc = "Semantic: 按类型+名查询单日志器内省（逐级别门控态与 proxyOn，同名歧义由 type 消除）；Returns: LoggerInfo 包；Failed=未命中";
inline constexpr const char* loggerAllDesc = "Semantic: 全量日志器内省（console 段在前 file 段在后，字典序）；Returns: LoggerInfoList 包";
inline constexpr const char* loggerFileGlobalStateDesc = "Semantic: 文件全局主闸态内省；Returns: LoggerState 包";
inline constexpr const char* loggerConsoleGlobalStateDesc = "Semantic: 控制台全局主闸态内省；Returns: LoggerState 包";
}  // namespace yomk::url