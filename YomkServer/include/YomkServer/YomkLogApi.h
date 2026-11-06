#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "YomkLogFmt.h"
#include "YomkMsgs.h"
#include "YomkPayload.h"
#include "YomkServerApi.h"
#include "YomkSourceLoc.h"
#include "YomkUrl.h"

// 日志域：发射（yomk::log()）与管理（yomk::log_mgr()）分域。发射链经请求域 .status()
// 三态收敛；管理操作无发射语义，入口不携带 YomkSourceLoc。emit 私有三件套
// （buildLogContent / emitConsoleLog / emitFileLog）自 YomkApi.h 迁入为终结 op 实现体；
// 24 个日志宏随本域落地退役——调用行位置捕获改由域入口默认实参承担
// （__builtin_FILE/LINE/FUNCTION 在默认实参表达式中求值时捕获调用方位置）。
/**
 * @brief 日志链式 Builder，发射与门控/内省/生命周期管理分域
 *
 * @par 主控制台发射
 * yomk::log().info(...) 等 ×6（info / warn / error / debug / trace / fatal）
 * @li 返回三态 ResStatus；未初始化返回 Invalid
 * @li 位置前缀在调用行捕获（默认仅行号 [N]，开关见"位置前缀"）
 * @li tag 缺省 MainLogger，.tag(t) 累积更换
 * @li 参数逐个追加（模板 pack，无上限），bool 输出 true/false
 *
 * @par 具名文件发射
 * yomk::log().to_sink(name).info(...) 等 ×6
 * @li .tag(t) 可叠加；to_sink 分面携带调用行位置
 *
 * @par 门控（管理域）
 * yomk::log_mgr().console(name).level(lv).enable() / .disable()
 * yomk::log_mgr().file(name).level(lv).enable() / .disable()
 * @li name 省略/空 = 全局主闸，具名 = 逐器门控（文件路径未知名返回 Failed）
 * @li .level(lv) 缺省 Info
 *
 * @par 代理与位置前缀（管理域）
 * @li yomk::log_mgr().console().proxy(fn).apply()：控制台代理，nullptr 恢复默认
 * @li yomk::log_mgr().file().proxy(fn).apply()：文件代理，nullptr 恢复默认落盘
 * @li yomk::log_mgr().location().show_file(on).show_line(on).show_func(on).apply()：
 *     位置前缀三开关（进程级，一次性落三开关）
 *
 * @par 文件日志器生命周期（管理域）
 * @li yomk::log_mgr().dir(dir).file(name).create()：创建文件日志器
 * @li yomk::log_mgr().file(name).flush()：缓冲落盘
 * @li yomk::log_mgr().file(name).remove()：删除日志器
 *
 * @par 内省（管理域）
 * @li yomk::log_mgr().console().list() / yomk::log_mgr().file().list()：
 *     该类型全部日志器名（StringArray，字典序）
 * @li yomk::log_mgr().console(name).get_info() / yomk::log_mgr().file(name).get_info()：
 *     指定日志器状态（两表允许同名，须按类型精确单查）
 * @li yomk::log_mgr().console().status() / yomk::log_mgr().file().status()：
 *     全局主闸状态（4 级别 + proxy，两套主闸相互独立）
 * @li yomk::log_mgr().all()：全部日志器及逐器门控状态（LoggerInfoList）
 *
 * @warning log() 域入口在调用行捕获位置，仅限单表达式内联使用，禁止缓存复用 Builder！
 */
// 公开面：仅域入口（约定 8：入口暴露、机制内敛，builder/分面类调用方全程 auto 接收、从不具名）
namespace yomk::detail
{
class LogBuilder;      // 前置声明，定义见下方机制段
class LogMgrBuilder;
}
namespace yomk
{
// 发射域入口：主控制台 / 具名文件发射（loc 经默认实参在调用行捕获，须单表达式内联）
inline detail::LogBuilder log(const YomkSourceLoc& loc = YomkSourceLoc{});  // 按值返回需完整类型，定义在机制段之后
// 管理域入口：门控 / 代理 / 位置前缀 / 文件日志器生命周期 / 内省
inline detail::LogMgrBuilder log_mgr();
}

// —— 机制段：builder 与分面（用户无需感知）——
namespace yomk::detail
{
// emit 正文组装：位置前缀 + 逐参追加，console/file 两个核心共用
template <typename... Args>
std::string buildLogContent(const YomkSourceLoc& loc, Args&&... args)
{
    std::string content;
    content.reserve(YomkLogFmt::logContentReserve());
    loc.appendTo(content);
    (YomkLogFmt::appendLogArg(content, std::forward<Args>(args)), ...);
    return content;
}
// 控制台 emit 核心：Log 的 name 与 tag 字段均为 tag
template <typename... Args>
yomk::payload::ResStatus emitConsoleLog(yomk::payload::LogLevel lv,
    const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
{
    return yomk::server().url(yomk::url::loggerConsoleLog).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), tag, tag})).status();
}
// 文件 emit 核心：Log 的 name 字段为 logFile
template <typename... Args>
yomk::payload::ResStatus emitFileLog(yomk::payload::LogLevel lv,
    const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
{
    return yomk::server().url(yomk::url::loggerFileLog).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), logFile, tag})).status();
}
// 逐级别开关公共核心：console/file × on/off × 六级别共用。
// loggerName 空 = 全局主闸（与既有无参语义一致）；具名 = 逐器门控，
// 文件路径未知名返回 ResStatus::Failed
inline yomk::payload::ResStatus setLogByLevel(
    bool console, bool on, yomk::payload::LogLevel lv, const std::string& loggerName)
{
    std::string_view url = console
        ? (on ? yomk::url::loggerOnConsoleLogByLevel : yomk::url::loggerOffConsoleLogByLevel)
        : (on ? yomk::url::loggerOnFileLogByLevel : yomk::url::loggerOffFileLogByLevel);
    return yomk::server().url(url).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, "", loggerName})).status();
}
// 发射分面：log().to_sink(name) 之后，持入口捕获的 loc + 具名文件日志器
class SinkLogBuilder
{
    YomkSourceLoc m_loc;
    std::string m_sink;
    std::string m_tag = "MainLogger";  // mainLoggerTag 内部化为此缺省值
public:
    SinkLogBuilder(const YomkSourceLoc& loc, std::string_view sink)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_loc(loc), m_sink(sink)
    {
    }
    // 累积 tag（缺省 MainLogger）
    SinkLogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    // —— 发射终端 ×6（模板 pack 逐参追加，log() 捕获的 loc 经 buildLogContent 加前缀）——
    template <typename... Args>
    yomk::payload::ResStatus info(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Info, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus warn(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Warn, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus error(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Error, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus debug(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Debug, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus trace(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Trace, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus fatal(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Fatal, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
};
// 发射域 builder：主控制台发射本体（无 to_sink 累积即主控制台路径）
class LogBuilder
{
    YomkSourceLoc m_loc;                 // 域入口默认参数捕获的调用行位置
    std::string m_tag = "MainLogger";    // mainLoggerTag 内部化为此缺省值
public:
    explicit LogBuilder(const YomkSourceLoc& loc)
        : m_loc(loc)
    {
    }
    // 累积 tag（缺省 MainLogger）
    LogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    // 具名文件发射分面（携带调用行位置）
    SinkLogBuilder to_sink(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return SinkLogBuilder(m_loc, name);
    }
    // —— 主控制台发射终端 ×6（模板 pack 逐参追加）——
    template <typename... Args>
    yomk::payload::ResStatus info(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Info, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus warn(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Warn, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus error(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Error, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus debug(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Debug, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus trace(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Trace, m_tag, m_loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    yomk::payload::ResStatus fatal(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Fatal, m_tag, m_loc, std::forward<Args>(args)...);
    }
};
// 文件日志器管理分面：log_mgr().file(name) 之后（level/proxy/内省/生命周期；
// name 即目标日志器名，空 = 全局主闸语义的载体）
class FileLogBuilder
{
    std::string m_file;
    std::string m_dir;                 // dir() 累积经 LogMgrBuilder::file() 携带（create 用）
    yomk::payload::LogLevel m_level = yomk::payload::LogLevel::Info;  // level() 累积，enable/disable 共用，缺省 Info
public:
    FileLogBuilder(std::string_view file, std::string_view dir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_file(file), m_dir(dir)
    {
    }
    // 累积门控级别（enable/disable 共用，缺省 Info）
    FileLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    // 开启（无参动作，读 level 累积；name 空 = 文件全局主闸，具名 = 逐器门控）
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(false, true, m_level, m_file);
    }
    // 关闭
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(false, false, m_level, m_file);
    }
    // 累积文件日志代理回调（约定 5：参数前置累积，终结 op 无参）
    FileLogBuilder& proxy(YomkFileLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    // 无参动作终结（落 proxy 累积，nullptr 恢复默认落盘；与控制台代理相互独立）
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetFileLogProxy).msg(yomk::msgs::FileLogProxy::shared(yomk::payload::FileLogProxy{m_proxy})).status();
    }
    // 该类型全部文件日志器名（StringArray，字典序）
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("file")).request();
    }
    // 查询指定文件日志器状态（两表允许同名，须按类型精确单查）
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_file, "file"})).request();
    }
    // 查询文件全局主闸状态（4 级别 + proxy）
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerFileGlobalState).request();
    }
    // 创建文件日志器（无参动作终结，读 dir + file 累积）
    yomk::payload::ResStatus create()
    {
        return yomk::server().url(yomk::url::loggerCreateFileLogger).msg(yomk::msgs::LogFile::shared(yomk::payload::LogFile{m_file, m_dir})).status();
    }
    // 将指定日志器的缓冲落盘
    yomk::payload::ResStatus flush()
    {
        return yomk::server().url(yomk::url::loggerWriteFileLog).msg(yomk::msgs::String::shared(m_file)).status();
    }
    // 删除指定日志器
    yomk::payload::ResStatus remove()
    {
        return yomk::server().url(yomk::url::loggerDeleteLogger).msg(yomk::msgs::String::shared(m_file)).status();
    }

private:
    YomkFileLogProxyFunc m_proxy;
};
// 控制台管理分面：log_mgr().console(name = "") 之后（控制台无发射路径，发射走 log 域）
class ConsoleLogBuilder
{
    std::string m_name;                // 空 = 全局主闸，具名 = 逐器门控
    yomk::payload::LogLevel m_level = yomk::payload::LogLevel::Info;  // level() 累积，enable/disable 共用，缺省 Info
    YomkConsoleLogProxyFunc m_proxy;
public:
    ConsoleLogBuilder(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_name(name)
    {
    }
    // 累积门控级别（enable/disable 共用，缺省 Info）
    ConsoleLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    // 开启（无参动作，读 level 累积；name 空 = 全局主闸，具名 = 逐器门控）
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(true, true, m_level, m_name);
    }
    // 关闭
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(true, false, m_level, m_name);
    }
    // 累积控制台日志代理回调
    ConsoleLogBuilder& proxy(YomkConsoleLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    // 无参动作终结（落 proxy 累积，nullptr 恢复默认输出）
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetConsoleProxy).msg(yomk::msgs::ConsoleLogProxy::shared(yomk::payload::ConsoleLogProxy{m_proxy})).status();
    }
    // 全部控制台日志器名（StringArray，字典序）
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("console")).request();
    }
    // 查询指定控制台日志器状态（两表允许同名，须按类型精确单查）
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_name, "console"})).request();
    }
    // 查询控制台全局主闸状态（4 级别 + proxy）
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerConsoleGlobalState).request();
    }
};
// 位置前缀开关分面：log_mgr().location() 之后，三开关累积（初值承接 YomkSourceLoc 当前状态）
// → apply() 终结；show_ 前缀命名避让与 .file(name) 的歧义
class LocSwitch
{
    bool m_file;   // 初值承接 YomkSourceLoc 当前状态（进程级开关）
    bool m_line;
    bool m_func;
public:
    LocSwitch()
        : m_file(YomkSourceLoc::fileFlag().load(std::memory_order_relaxed)),
          m_line(YomkSourceLoc::lineFlag().load(std::memory_order_relaxed)),
          m_func(YomkSourceLoc::funcFlag().load(std::memory_order_relaxed))
    {
    }
    // 累积 file 名展示开关
    LocSwitch& show_file(bool on)
    {
        m_file = on;
        return *this;
    }
    // 累积行号展示开关
    LocSwitch& show_line(bool on)
    {
        m_line = on;
        return *this;
    }
    // 累积函数名展示开关
    LocSwitch& show_func(bool on)
    {
        m_func = on;
        return *this;
    }
    // 无参动作终结（进程级生效，线程安全，一次性落三开关）
    yomk::payload::ResStatus apply()
    {
        YomkSourceLoc::fileFlag().store(m_file);
        YomkSourceLoc::lineFlag().store(m_line);
        YomkSourceLoc::funcFlag().store(m_func);
        return yomk::payload::ResStatus::Succeeded;
    }
};
// 管理域 builder：分面入口 + 跨分面累积（dir）+ 全局内省
class LogMgrBuilder
{
    std::string m_dir;
public:
    // 累积日志目录（create 用，须先于 file() 累积）
    LogMgrBuilder& dir(std::string_view logDir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_dir = logDir;
        return *this;
    }
    // 文件日志器管理分面（携带 dir 累积；name 空 = 全局主闸语义，proxy/status/list 不用名）
    FileLogBuilder file(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return FileLogBuilder(name, m_dir);
    }
    // 控制台管理分面（name 空 = 全局主闸）
    ConsoleLogBuilder console(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return ConsoleLogBuilder(name);
    }
    // 位置前缀开关分面
    LocSwitch location()
    {
        return LocSwitch{};
    }
    // 全部日志器及逐器门控状态（LoggerInfoList，console/file 条目为各自逐器门控层真值）
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerAll).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
inline detail::LogBuilder log(const YomkSourceLoc& loc)
{
    return detail::LogBuilder(loc);
}
inline detail::LogMgrBuilder log_mgr()
{
    return detail::LogMgrBuilder{};
}
}  // namespace yomk
