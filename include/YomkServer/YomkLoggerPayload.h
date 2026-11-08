#pragma once
#include <functional>
#include <string>

#include "YomkPayload.h"

namespace yomk::payload
{

enum class LogLevel : int
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

struct Log
{
    LogLevel m_level;
    std::string m_log;
    std::string m_logger;  // 日志器名：服务端按其查找/惰性创建日志器实例
    std::string m_tag;  // 日志标签：随记录显示 [tag] 段，区别于 m_logger
};

struct LogFile
{
    std::string m_logger;
    std::string m_dir;
};

struct ConsoleLogProxy
{
    std::function<bool(const Log& log)> m_consoleLogProxyFunc;
};

struct FileLogProxy
{
    std::function<bool(const Log& log)> m_fileLogProxyFunc;
};

struct LoggerQuery
{
    std::string m_loggerName;
    std::string m_type;
};

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

struct LoggerInfo
{
    std::string m_loggerName;  // 日志器名
    std::string m_type;        // 类型："console" 或 "file"（对应 log_mgr() 的 console/file 分面身份）
    std::string m_dir;         // file 类型的日志目录（console 时为空串）
    LoggerState m_loggerState;
};

}  // namespace yomk::payload

/**
 * @brief 控制台日志代理回调
 * @param log 待输出的日志条目
 * @return false 拦截该条不输出、true 穿透走默认输出
 * @note 详见 yomk::log_mgr().console().proxy(fn).apply()
 */
using YomkConsoleLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
/**
 * @brief 文件日志代理回调
 * @param log 待落盘的日志条目
 * @return false 拦截该条不落盘、true 穿透走默认落盘
 * @note 详见 yomk::log_mgr().file().proxy(fn).apply()
 */
using YomkFileLogProxyFunc = std::function<bool(const yomk::payload::Log& log)>;
