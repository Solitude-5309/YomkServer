#pragma once
#include <string_view>

// /YomkLogger/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// 常量名 = k + 域前缀驼峰 + 端点驼峰（kCtxCreate）；语义描述常量同名 + Desc（kCtxCreateDesc）。规范见 docs/yomk命名原则.md
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkLogger/*
inline constexpr std::string_view kLoggerSetConsoleProxy = "/YomkLogger/set_console_log_proxy";
inline constexpr std::string_view kLoggerSetFileLogProxy = "/YomkLogger/set_file_log_proxy";
inline constexpr std::string_view kLoggerConsoleLog = "/YomkLogger/console_log";
inline constexpr std::string_view kLoggerCreateFileLogger = "/YomkLogger/create_file_logger";
inline constexpr std::string_view kLoggerFileLog = "/YomkLogger/file_log";
inline constexpr std::string_view kLoggerWriteFileLog = "/YomkLogger/write_file_log";
inline constexpr std::string_view kLoggerDeleteLogger = "/YomkLogger/delete_logger";
inline constexpr std::string_view kLoggerOnConsoleLogByLevel = "/YomkLogger/on_console_log_by_level";
inline constexpr std::string_view kLoggerOffConsoleLogByLevel = "/YomkLogger/off_console_log_by_level";
inline constexpr std::string_view kLoggerOnFileLogByLevel = "/YomkLogger/on_file_log_by_level";
inline constexpr std::string_view kLoggerOffFileLogByLevel = "/YomkLogger/off_file_log_by_level";
inline constexpr std::string_view kLoggerLoggers = "/YomkLogger/loggers";
inline constexpr std::string_view kLoggerLogger = "/YomkLogger/logger";
inline constexpr std::string_view kLoggerAll = "/YomkLogger/all";
inline constexpr std::string_view kLoggerFileGlobalState = "/YomkLogger/file_global_state";
inline constexpr std::string_view kLoggerConsoleGlobalState = "/YomkLogger/console_global_state";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* kLoggerSetConsoleProxyDesc = R"(
# Overview
安装控制台输出代理：全局唯一，任意 console 日志器发射的每条记录都先过该回调。

# Inputs
ConsoleLogProxy 包：consoleLogProxyFunc 回调（const Log& → bool）；传空回调即卸载代理并恢复默认 stdout 输出。

# Outputs
data 无负载，msg "success"。

# Behavior
同步安装：锁内替换回调快照；回调在日志发射线程同步执行（锁内取快照、锁外调用），返回 false 拦截该条、true 穿透默认输出。

# Notes
代理是全局而非逐器；回调应快速返回，勿在回调内再发日志以免递归；重复安装为覆盖（幂等）；请求消息非 ConsoleLogProxy → Failed。
)";
inline constexpr const char* kLoggerSetFileLogProxyDesc = R"(
# Overview
安装文件输出代理：全局唯一，任意 file 日志器发射的每条记录都先过该回调。

# Inputs
FileLogProxy 包：fileLogProxyFunc 回调（const Log& → bool）；传空回调即卸载代理并恢复默认落盘。

# Outputs
data 无负载，msg "success"。

# Behavior
同步安装：锁内替换回调快照；回调在日志发射线程同步执行，返回 false 拦截该条、true 穿透默认落盘。

# Notes
代理先于日志器表查找，故拦截态下未知名日志器的记录同样被短路；与控制台代理互相独立；重复安装为覆盖（幂等）；请求消息非 FileLogProxy → Failed。
)";
inline constexpr const char* kLoggerConsoleLogDesc = R"(
# Overview
写一条控制台日志：先过控制台代理，再经"全局主闸 × 逐器级别门控"两级 AND 决定是否输出到 stdout。

# Inputs
Log 包：level 级别；log 正文；tag 标签（输出为 [tag] 段）；logger 日志器名（空串按 "main" 处理并 WARN）。

# Outputs
data 无负载；被代理拦截时 msg "ok"，否则 msg "success"。

# Behavior
同步执行：代理判定 → 日志器查找（未命中按名惰性创建控制台日志器并登记）→ 级别门控 → 输出。

# Notes
未知 level 回退 Info 并 WARN；控制台与文件两套代理、两套门控互相独立；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerCreateFileLoggerDesc = R"(
# Overview
创建文件日志器并初始化落盘文件：注册后该名即可用于 /file_log 缓冲写入。

# Inputs
LogFile 包：logger 日志器名；dir 落盘目录（不存在时自动创建）。

# Outputs
data 无负载，msg "success"。

# Behavior
同步执行：创建目录与 <dir>/<logger>.log 文件并登记到文件日志器表；四级日志先入内存缓冲，落盘由 /write_file_log 触发。

# Notes
非幂等——重名创建必 Failed；Invalid=名/目录为空；Failed=重名/初始化失败；请求消息非 LogFile → Failed。
)";
inline constexpr const char* kLoggerFileLogDesc = R"(
# Overview
向指定文件日志器缓冲写入一条日志；不落盘，需 /write_file_log 触发。

# Inputs
Log 包：logger 日志器名（不可为空）；level/log/tag 语义同控制台侧。

# Outputs
data 无负载；被代理拦截时 msg "ok"，否则 msg "success"。

# Behavior
同步执行：文件代理判定 → 表查找（不惰性创建）→ 全局主闸 × 逐器门控 → 追加到该日志器内存缓冲。

# Notes
日志器不存在 → Failed；logger 为空 → Failed；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerWriteFileLogDesc = R"(
# Overview
把指定文件日志器的内存缓冲落盘并清空缓冲。

# Inputs
String 包，d 为日志器名。

# Outputs
data 无负载，msg "success"。

# Behavior
同步执行：追加写文件后清空内存缓冲；空缓冲重复调用无副作用（幂等）。

# Notes
日志器不存在 → Failed；落盘 I/O 在本端点调用线程内完成；请求消息非 String → Failed。
)";
inline constexpr const char* kLoggerDeleteLoggerDesc = R"(
# Overview
删除指定名字的日志器：控制台表与文件表各删一次同名项。

# Inputs
String 包，d 为日志器名。

# Outputs
data 无负载，msg "deleted console:<N> file:<M>"（N/M 为各表实际删除条数）。

# Behavior
同步执行：分段持两把写锁不嵌套，文件日志器对象锁外析构以保证析构期落盘 I/O 不阻塞其它日志写入；两表都不存在同名项时 Failed。

# Notes
不删除磁盘上的 .log 文件（清理归调用方）；名空 → Invalid；"main" 控制台日志器也可被删除，之后再次发日志会按名重建；请求消息非 String → Failed。
)";
inline constexpr const char* kLoggerOnConsoleLogByLevelDesc = R"(
# Overview
打开 Log 包指定级别的控制台输出门控：logger 为空改全局主闸，非空改指定控制台日志器的逐器门控。

# Inputs
Log 包：level 目标级别（必填）；logger 日志器名（空=全局主闸路径，非空=逐器路径）；log/tag 忽略。

# Outputs
data 无负载，msg "success"（未知 level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级 atomic；逐器路径表读锁取指针后锁外 setLevelOn；幂等。

# Notes
逐器路径不惰性创建，日志器不存在 → Failed；两级 AND 生效，任一级关闭即不输出；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerOffConsoleLogByLevelDesc = R"(
# Overview
关闭 Log 包指定级别的控制台输出门控：logger 为空改全局主闸，非空改指定控制台日志器的逐器门控。

# Inputs
Log 包：level 目标级别（必填）；logger 日志器名（空=全局主闸路径，非空=逐器路径）；log/tag 忽略。

# Outputs
data 无负载，msg "success"（未知 level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级 atomic；逐器路径表读锁取指针后锁外 setLevelOn；幂等。

# Notes
逐器路径不惰性创建，日志器不存在 → Failed；两级 AND 生效，任一级关闭即不输出；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerOnFileLogByLevelDesc = R"(
# Overview
打开 Log 包指定级别的文件输出门控：logger 为空改全局主闸，非空改指定文件日志器的逐器门控。

# Inputs
Log 包：level 目标级别（必填）；logger 日志器名（空=全局主闸路径，非空=逐器路径）；log/tag 忽略。

# Outputs
data 无负载，msg "success"（未知 level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级 atomic；逐器路径表读锁取指针后锁外 setLevelOn；幂等。

# Notes
文件侧主闸与控制台侧互相独立；逐器路径不惰性创建，日志器不存在 → Failed；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerOffFileLogByLevelDesc = R"(
# Overview
关闭 Log 包指定级别的文件输出门控：logger 为空改全局主闸，非空改指定文件日志器的逐器门控。

# Inputs
Log 包：level 目标级别（必填）；logger 日志器名（空=全局主闸路径，非空=逐器路径）；log/tag 忽略。

# Outputs
data 无负载，msg "success"（未知 level 亦返回 Succeeded，仅 WARN 留痕）。

# Behavior
同步执行：全局路径改写进程级 atomic；逐器路径表读锁取指针后锁外 setLevelOn；幂等。

# Notes
文件侧主闸与控制台侧互相独立；逐器路径不惰性创建，日志器不存在 → Failed；请求消息非 Log → Failed。
)";
inline constexpr const char* kLoggerLoggersDesc = R"(
# Overview
按类型列出日志器名清单（控制台表或文件表）。

# Inputs
String 包，d 必须是 "console" 或 "file"。

# Outputs
data 为 StringArray 包（map 字典序），msg "ok"。

# Behavior
同步快照读取，持对应表读锁拷贝；幂等。

# Notes
其他取值 → Failed（msg 带实际类型值）；请求消息非 String → Failed。
)";
inline constexpr const char* kLoggerLoggerDesc = R"(
# Overview
按类型 + 名查询单日志器内省快照：逐器六级别门控态、proxyOn、文件日志器目录。

# Inputs
LoggerQuery 包：loggerName 日志器名；type "console" 或 "file"（两表允许同名，由 type 消歧）。

# Outputs
data 为 LoggerInfo 包（loggerName/type/dir/loggerState），msg "ok"。

# Behavior
同步执行：按类型单表精确查找，锁内拷贝指针后锁外构造快照；幂等。

# Notes
loggerState 六级别为逐器门控真值（非全局主闸），proxyOn 为该侧全局代理态；type 未知或名未命中 → Failed；请求消息非 LoggerQuery → Failed。
)";
inline constexpr const char* kLoggerAllDesc = R"(
# Overview
全量日志器内省快照：控制台段在前、文件段在后。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerInfoList 包（段内按 map 字典序），msg "ok"。

# Behavior
同步执行：先在表锁外分别取两侧全局 proxyOn，再按锁序嵌套双读锁取跨段快照；幂等。

# Notes
跨两表快照非原子一致；任一侧为空时仅少该段，不返回 Failed。
)";
inline constexpr const char* kLoggerFileGlobalStateDesc = R"(
# Overview
文件全局主闸态内省：六级别开关 + 文件代理是否已装。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerState 包（traceOn/debugOn/infoOn/warnOn/errorOn/fatalOn/proxyOn），msg "ok"。

# Behavior
同步读取进程级 atomic 与代理叶子锁；幂等。

# Notes
六级别为独立 atomic，组合结果不构成原子快照（并发切换中可能读到新旧混合）；与控制台主闸互相独立。
)";
inline constexpr const char* kLoggerConsoleGlobalStateDesc = R"(
# Overview
控制台全局主闸态内省：六级别开关 + 控制台代理是否已装。

# Inputs
无（不带请求包）。

# Outputs
data 为 LoggerState 包（traceOn/debugOn/infoOn/warnOn/errorOn/fatalOn/proxyOn），msg "ok"。

# Behavior
同步读取进程级 atomic 与代理叶子锁；幂等。

# Notes
六级别为独立 atomic，组合结果不构成原子快照（并发切换中可能读到新旧混合）；与文件主闸互相独立。
)";
}  // namespace yomk::url
