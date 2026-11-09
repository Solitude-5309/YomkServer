#pragma once
#include <functional>
#include <string>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

/**
 * @brief Log levels: severity from low to high, one-to-one with the six emit methods
 */
// @zh: 日志级别：严重程度自低到高，与六级发射方法一一对应
enum class LogLevel : int
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

/** @brief Single log record payload (shared by console/file emission) */
// @zh: 单条日志记录载荷（console/file 发射共用）
struct Log
{
    LogLevel m_level;
    std::string m_log;
    std::string m_logger;  // 日志器名:服务端按其查找/惰性创建日志器实例
    std::string m_tag;  // 日志标签:随记录显示 [tag] 段,区别于 m_logger
};

/** @brief File logger creation payload (directory + name) */
// @zh: 文件日志器创建载荷（目录 + 名称）
struct LogFile
{
    std::string m_logger;
    std::string m_dir;
};

/** @brief Console proxy registration payload */
// @zh: 控制台代理注册载荷
struct ConsoleLogProxy
{
    std::function<bool(const Log& log)> m_consoleLogProxyFunc;
};

/** @brief File proxy registration payload */
// @zh: 文件代理注册载荷
struct FileLogProxy
{
    std::function<bool(const Log& log)> m_fileLogProxyFunc;
};

/** @brief Single-logger query payload (name + type) */
// @zh: 日志器单查载荷（名称 + 类型）
struct LoggerQuery
{
    std::string m_loggerName;
    std::string m_type;
};

/** @brief Gating state snapshot (global master switch or per-logger level) */
// @zh: 门控状态快照（全局主闸或逐器层）
struct LoggerState
{
    bool m_traceOn = false;  // trace 级别开关
    bool m_debugOn = false;  // debug 级别开关
    bool m_infoOn = false;   // info 级别开关
    bool m_warnOn = false;   // warn 级别开关
    bool m_errorOn = false;  // error 级别开关
    bool m_fatalOn = false;  // fatal 级别开关
    bool m_proxyOn = false;
};

/** @brief Single logger introspection entry */
// @zh: 单条日志器内省条目
struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型:"console" 或 "file"(对应 log_mgr() 的 console/file 分面身份)
    std::string m_dir;         // file 类型的日志目录(console 时为空串)
    LoggerState m_loggerState;
};

}  // namespace yomk::payload

/**
 * @brief Console log proxy callback
 * @param log Log record to be output
 * @return false blocks this record from output, true passes through to the default output
 * @note Called synchronously on the log emission thread, the callback should return quickly; see yomk::log_mgr().console().proxy(fn).apply() for details
 */
// @zh: 控制台日志代理回调
// @zh: log 待输出的日志条目
// @zh: return false 拦截该条不输出，true 穿透走默认输出
// @zh: note 在日志发射线程同步调用，回调应快速返回；详见 yomk::log_mgr().console().proxy(fn).apply()
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
/**
 * @brief File log proxy callback
 * @param log Log record to be written to disk
 * @return false blocks this record from being written to disk, true passes through to the default disk write
 * @note Called synchronously on the log emission thread, the callback should return quickly; see yomk::log_mgr().file().proxy(fn).apply() for details
 */
// @zh: 文件日志代理回调
// @zh: log 待落盘的日志条目
// @zh: return false 拦截该条不落盘，true 穿透走默认落盘
// @zh: note 在日志发射线程同步调用，回调应快速返回；详见 yomk::log_mgr().file().proxy(fn).apply()
using YomkFileLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
