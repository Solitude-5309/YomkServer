#pragma once
#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "yomk/log/YomkLogSourceLoc.h"
#include "yomk/log/YomkLoggerMsg.h"
#include "yomk/log/YomkLoggerPayload.h"
#include "yomk/log/YomkLoggerUrl.h"
#include "yomk/msg/YomkStdMsg.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerPayload.h"

// 日志域设计:调用行位置捕获由域入口默认实参承担——__builtin_FILE/LINE/FUNCTION 在默认实参表达式求值时捕获调用方位置;
// log() 携带 YomkSourceLoc,log_mgr() 无发射语义不携带.
/**
 * @brief Chained log builder, emission and gating/introspection/lifecycle management in separate domains
 *
 * @par Main console emission
 * yomk::log().info(...) etc., x6 (info / warn / error / debug / trace / fatal)
 * @li Returns tri-state ResStatus; returns Invalid if uninitialized
 * @li Location prefix captured on the calling line (line number only [N] by default, see "location prefix" for the switches)
 * @li tag defaults to main, .tag(t) accumulates and replaces
 * @li Arguments appended one by one (template pack, no upper limit), bool prints true/false
 *
 * @par Named file emission
 * yomk::log().to_sink(name).info(...) etc., x6
 * @li .tag(t) stackable; the to_sink facet carries the calling-line location
 *
 * @par Gating (management domain)
 * yomk::log_mgr().console(name).level(lv).enable() / .disable()
 * yomk::log_mgr().file(name).level(lv).enable() / .disable()
 * @li Omitted/empty name = global master switch, named = per-logger gating (unknown file path name returns Failed)
 * @li .level(lv) defaults to Info
 *
 * @par Proxy and location prefix (management domain)
 * @li yomk::log_mgr().console().proxy(fn).apply(): console proxy, nullptr restores the default
 * @li yomk::log_mgr().file().proxy(fn).apply(): file proxy, nullptr restores default disk write
 * @li yomk::log_mgr().location().show_file(on).show_line(on).show_func(on).apply():
 *     three location-prefix switches (process-level, all three set at once)
 *
 * @par File logger lifecycle (management domain)
 * @li yomk::log_mgr().dir(dir).file(name).create(): create a file logger
 * @li yomk::log_mgr().file(name).flush(): flush buffered logs to disk
 * @li yomk::log_mgr().file(name).remove(): remove the logger
 *
 * @par Introspection (management domain)
 * @li yomk::log_mgr().console().list() / yomk::log_mgr().file().list():
 *     all logger names of that type (StringArray, lexicographic order)
 * @li yomk::log_mgr().console(name).get_info() / yomk::log_mgr().file(name).get_info():
 *     specified logger state (the two tables allow duplicate names, must query exactly by type)
 * @li yomk::log_mgr().console().status() / yomk::log_mgr().file().status():
 *     global master switch state (6 levels + proxy, the two master switches are independent)
 * @li yomk::log_mgr().all(): all loggers with per-logger gating state (LoggerInfoList)
 *
 * @warning The log() domain entry captures the location on the calling line, use inline in a single expression only, never cache or reuse the builder!
 */
// @zh: 日志链式 Builder，发射与门控/内省/生命周期管理分域
// @zh: 主控制台发射
// @zh: yomk::log().info(...) 等 ×6（info / warn / error / debug / trace / fatal）
// @zh: 返回三态 ResStatus；未初始化返回 Invalid
// @zh: 位置前缀在调用行捕获（默认仅行号 [N]，开关见"位置前缀"）
// @zh: tag 缺省 main，.tag(t) 累积更换
// @zh: 参数逐个追加（模板 pack，无上限），bool 输出 true/false
// @zh: 具名文件发射
// @zh: yomk::log().to_sink(name).info(...) 等 ×6
// @zh: .tag(t) 可叠加；to_sink 分面携带调用行位置
// @zh: 门控（管理域）
// @zh: name 省略/空 = 全局主闸，具名 = 逐器门控（文件路径未知名返回 Failed）
// @zh: .level(lv) 缺省 Info
// @zh: 代理与位置前缀（管理域）
// @zh: yomk::log_mgr().console().proxy(fn).apply()：控制台代理，nullptr 恢复默认
// @zh: yomk::log_mgr().file().proxy(fn).apply()：文件代理，nullptr 恢复默认落盘
// @zh: yomk::log_mgr().location().show_file(on).show_line(on).show_func(on).apply()：位置前缀三开关（进程级，一次性落三开关）
// @zh: 文件日志器生命周期（管理域）
// @zh: yomk::log_mgr().dir(dir).file(name).create()：创建文件日志器
// @zh: yomk::log_mgr().file(name).flush()：缓冲落盘
// @zh: yomk::log_mgr().file(name).remove()：删除日志器
// @zh: 内省（管理域）
// @zh: yomk::log_mgr().console().list() / yomk::log_mgr().file().list()：该类型全部日志器名（StringArray，字典序）
// @zh: yomk::log_mgr().console(name).get_info() / yomk::log_mgr().file(name).get_info()：指定日志器状态（两表允许同名，须按类型精确单查）
// @zh: yomk::log_mgr().console().status() / yomk::log_mgr().file().status()：全局主闸状态（6 级别 + proxy，两套主闸相互独立）
// @zh: yomk::log_mgr().all()：全部日志器及逐器门控状态（LoggerInfoList）
// @zh: warning log() 域入口在调用行捕获位置，仅限单表达式内联使用，禁止缓存复用 Builder！
// 公开面仅暴露域入口, builder/分面类机制内敛, 调用方全程以 auto 接收,从不具名引用类型
namespace yomk::detail
{
class LogBuilder;      // 前置声明, 定义见下方机制段
class LogMgrBuilder;
}
namespace yomk
{
/**
 * @brief Log emission domain entry: main console / named file emission
 * @note Location captured on the calling line, use inline in a single expression
 * @note Thread-safe, non-blocking
 * @return LogBuilder (see the class documentation for details)
 */
// @zh: 日志发射域入口：主控制台 / 具名文件发射
// @zh: note 位置在调用行捕获，须单表达式内联使用
// @zh: note 线程安全，非阻塞
// @zh: return LogBuilder（详见类文档）
inline detail::LogBuilder log(const YomkSourceLoc& loc = YomkSourceLoc{});  // 按值返回需完整类型, 定义在机制段之后
/**
 * @brief Log management domain entry: gating / proxy / location prefix / file logger lifecycle / introspection
 * @note Thread-safe, non-blocking
 * @return LogMgrBuilder (see the class documentation for details)
 */
// @zh: 日志管理域入口：门控 / 代理 / 位置前缀 / 文件日志器生命周期 / 内省
// @zh: note 线程安全，非阻塞
// @zh: return LogMgrBuilder（详见类文档）
inline detail::LogMgrBuilder log_mgr();
}

// —— 机制段: builder 与分面 ——
namespace yomk::detail
{
/**
 * @brief Log content formatting: single implementation of initial reserved capacity plus per-argument appending
 * @note Established output: arguments separated by a single space, bool prints true/false, integers/floats match the ostream defaults;
 *     decoupled from the chained surface, also reusable for assembling log content externally
 */
// @zh: 日志正文格式化：起始预留容量 + 逐参追加的单一实现
// @zh: note 输出定版：实参间以单个空格分隔，bool 输出 true/false，整型/浮点与 ostream 默认一致；与链式表面无耦合，外部自行拼接日志 content 亦可复用
struct YomkLogFmt
{
    // emit 正文起始预留容量:典型日志单行 64 字节内,预分配减少 string 扩容
    static std::size_t logContentReserve() { static const std::size_t v = 64; return v; }

    // 逐参追加:每个实参前补一个空格.
    // 常用类型直拼, 整型手写 itoa, 浮点 snprintf, 其余可流输出类型回退 ostringstream.
    // bool 刻意输出 true/false(用户定版), 不沿用 ostream 默认的 1/0
    static void appendLogArg(std::string& out, const char* v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, const std::string& v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, char v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, bool v) { out += ' '; out += (v ? "true" : "false"); }
    template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // 手写 itoa:负号 + 十进制数字,与 ostream 默认输出一致.负数 magnitude 在
        // 无符号域取 0 - raw(对最小值取负是 UB);符号判定 is_signed 编译期短路,
        // 无符号类型不做回读比较
        using U = typename std::make_unsigned<T>::type;
        const U raw = static_cast<U>(v);
        const bool neg = std::is_signed<T>::value &&
                         static_cast<typename std::make_signed<U>::type>(raw) < 0;
        U u = neg ? static_cast<U>(0) - raw : raw;
        constexpr std::size_t kDigitBufSize = 24; // uint64 十进制至多 20 位,24 含余量
        constexpr unsigned kDecBase = 10;
        std::array<char, kDigitBufSize> digits = {};
        std::size_t count = 0;
        do
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            digits[count++] = static_cast<char>('0' + u % kDecBase);
            u /= kDecBase;
        } while (u != 0);
        if (neg)
        {
            out += '-';
        }
        while (count > 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            out += digits[--count];
        }
    }
    template <typename T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // %g(6 位有效数字)与 ostream 默认输出一致;long double 需 %Lg.
        // 栈缓冲 64 字节足够(最长形如 -1.23457e-308)
        constexpr std::size_t kFltBufSize = 64;
        std::array<char, kFltBufSize> buf = {};
        int n = 0;
        if (std::is_same<T, long double>::value)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            n = std::snprintf(buf.data(), buf.size(), "%Lg", static_cast<long double>(v));
        }
        else
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            n = std::snprintf(buf.data(), buf.size(), "%g", static_cast<double>(v));
        }
        if (n > 0)
        {
            out.append(buf.data(), static_cast<std::size_t>(n));
        }
    }
    // 回退重载:非算术,且不可转 const char*/std::string 的任意可流输出类型
    // (enable_if 条件与非模板重载集互斥)
    template <typename T,
        typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_convertible<T, const char*>::value
                                    && !std::is_convertible<T, const std::string&>::value,
            int>::type = 0>
    static void appendLogArg(std::string& out, const T& v)
    {
        std::ostringstream oss;
        oss << v;
        out += ' ';
        out += oss.str();
    }
};

// emit 正文组装: 位置前缀 + 逐参追加, console/file 两个核心共用
template <typename... Args>
std::string buildLogContent(const YomkSourceLoc& loc, Args&&... args)
{
    std::string content;
    content.reserve(YomkLogFmt::logContentReserve());
    loc.appendTo(content);
    (YomkLogFmt::appendLogArg(content, std::forward<Args>(args)), ...);
    return content;
}
// 控制台 emit 核心: Log 的 name 与 tag 字段均为 tag
template <typename... Args>
yomk::payload::ResStatus emitConsoleLog(yomk::payload::LogLevel lv,
    const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
{
    return yomk::server().url(yomk::url::loggerConsoleLog).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), tag, tag})).status();
}
// 文件 emit 核心: Log 的 name 字段为 logFile
template <typename... Args>
yomk::payload::ResStatus emitFileLog(yomk::payload::LogLevel lv,
    const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
{
    return yomk::server().url(yomk::url::loggerFileLog).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), logFile, tag})).status();
}
// 逐级别开关公共核心: console/file × on/off × 六级别共用.
// loggerName 空 = 全局主闸; 具名 = 逐器门控, 文件路径未知名返回 ResStatus::Failed
inline yomk::payload::ResStatus setLogByLevel(
    bool console, bool on, yomk::payload::LogLevel lv, const std::string& loggerName)
{
    std::string_view url = console
        ? (on ? yomk::url::loggerOnConsoleLogByLevel : yomk::url::loggerOffConsoleLogByLevel)
        : (on ? yomk::url::loggerOnFileLogByLevel : yomk::url::loggerOffFileLogByLevel);
    return yomk::server().url(url).msg(yomk::msgs::Log::shared(yomk::payload::Log{lv, "", loggerName})).status();
}

/**
 * @brief Named file emission facet: keep accumulating after log().to_sink(name), terminate with a level method
 * @note The six level emit terminals block until the log write completes, return Invalid if uninitialized; the location prefix is captured from the log() calling line
 */
// @zh: 具名文件发射分面：log().to_sink(name) 之后继续累积并以级别方法终结
// @zh: note 六级别发射终端阻塞至日志写入完成，未初始化返回 Invalid；位置前缀捕获自 log() 调用行
class SinkLogBuilder
{
    YomkSourceLoc m_loc;
    std::string m_sink;
    std::string m_tag = "main";
public:
    SinkLogBuilder(const YomkSourceLoc& loc, std::string_view sink)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_loc(loc), m_sink(sink)
    {
    }
    /**
     * @brief Accumulate log tag
     * @param t Tag name, default main
     * @return Reference to self for chaining
     */
    // @zh: 累积日志 tag
    // @zh: t tag 名，缺省 main
    // @zh: return 自身链式引用
    SinkLogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    /** @brief Emit at Info level and terminate the chain */
    // @zh: 以 Info 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus info(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Info, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Warn level and terminate the chain */
    // @zh: 以 Warn 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus warn(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Warn, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Error level and terminate the chain */
    // @zh: 以 Error 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus error(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Error, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Debug level and terminate the chain */
    // @zh: 以 Debug 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus debug(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Debug, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Trace level and terminate the chain */
    // @zh: 以 Trace 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus trace(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Trace, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Fatal level and terminate the chain */
    // @zh: 以 Fatal 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus fatal(Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Fatal, m_sink, m_tag, m_loc, std::forward<Args>(args)...);
    }
};

/**
 * @brief Emission domain builder: main console emission body (the main console path without going through the to_sink facet)
 * @note The six level emit terminals block until the log write completes, return Invalid if uninitialized; the location prefix is captured from the log() calling line
 */
// @zh: 发射域 builder：主控制台发射本体（不经 to_sink 分面即主控制台路径）
// @zh: note 六级别发射终端阻塞至日志写入完成，未初始化返回 Invalid；位置前缀捕获自 log() 调用行
class LogBuilder
{
    YomkSourceLoc m_loc;                 // 域入口默认参数捕获的调用行位置
    std::string m_tag = "main";
public:
    explicit LogBuilder(const YomkSourceLoc& loc)
        : m_loc(loc)
    {
    }
    /**
     * @brief Accumulate log tag
     * @param t Tag name, default main
     * @return Reference to self for chaining
     */
    // @zh: 累积日志 tag
    // @zh: t tag 名，缺省 main
    // @zh: return 自身链式引用
    LogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    /**
     * @brief Switch into the named file emission facet
     * @param name Target file logger name
     * @return File emission facet (carries this calling-line location)
     * @warning Unknown-name writes return Failed, no lazy creation
     */
    // @zh: 切入具名文件发射分面
    // @zh: name 目标文件日志器名
    // @zh: return 文件发射分面（携带本调用行位置）
    // @zh: warning 未知名写入返回 Failed，不惰性创建
    SinkLogBuilder to_sink(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return SinkLogBuilder(m_loc, name);
    }
    /** @brief Emit at Info level and terminate the chain */
    // @zh: 以 Info 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus info(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Info, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Warn level and terminate the chain */
    // @zh: 以 Warn 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus warn(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Warn, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Error level and terminate the chain */
    // @zh: 以 Error 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus error(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Error, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Debug level and terminate the chain */
    // @zh: 以 Debug 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus debug(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Debug, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Trace level and terminate the chain */
    // @zh: 以 Trace 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus trace(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Trace, m_tag, m_loc, std::forward<Args>(args)...);
    }
    /** @brief Emit at Fatal level and terminate the chain */
    // @zh: 以 Fatal 级别发射并终结
    template <typename... Args>
    yomk::payload::ResStatus fatal(Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Fatal, m_tag, m_loc, std::forward<Args>(args)...);
    }
};

/**
 * @brief File logger management facet: after log_mgr().file(name),
 * carries gating / proxy / introspection / lifecycle termination
 */
// @zh: 文件日志器管理分面：log_mgr().file(name) 之后
// @zh: 承载门控 / 代理 / 内省 / 生命周期终结
class FileLogBuilder
{
    std::string m_file;
    std::string m_dir;                 // dir() 累积经 LogMgrBuilder::file() 携带(create 用)
    yomk::payload::LogLevel m_level = yomk::payload::LogLevel::Info;  // level() 累积, enable/disable 共用, 缺省 Info
public:
    FileLogBuilder(std::string_view file, std::string_view dir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_file(file), m_dir(dir)
    {
    }
    /**
     * @brief Accumulate gating level (shared by enable/disable)
     * @param lv Target level, default Info
     * @return Reference to self for chaining
     */
    // @zh: 累积门控级别（enable/disable 共用）
    // @zh: lv 目标级别，缺省 Info
    // @zh: return 自身链式引用
    FileLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    /**
     * @brief Enable gating, terminates the chain
     * @return Tri-state ResStatus; unknown file path name returns Failed
     * @note Empty name = file global master switch, named = per-logger gating
     */
    // @zh: 开启门控终结
    // @zh: return 三态 ResStatus；文件路径未知名返回 Failed
    // @zh: note name 空 = 文件全局主闸，具名 = 逐器门控
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(false, true, m_level, m_file);
    }
    /**
     * @brief Disable gating, terminates the chain
     * @return Tri-state ResStatus; unknown file path name returns Failed
     * @note Empty name = file global master switch, named = per-logger gating
     */
    // @zh: 关闭门控终结
    // @zh: return 三态 ResStatus；文件路径未知名返回 Failed
    // @zh: note name 空 = 文件全局主闸，具名 = 逐器门控
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(false, false, m_level, m_file);
    }
    /**
     * @brief Accumulate file log proxy callback
     * @param fn Proxy callback, nullptr = restore default disk write
     * @return Reference to self for chaining
     */
    // @zh: 累积文件日志代理回调
    // @zh: fn 代理回调，nullptr = 恢复默认落盘
    // @zh: return 自身链式引用
    FileLogBuilder& proxy(YomkFileLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    /**
     * @brief Proxy commit termination
     * @return Tri-state ResStatus
     * @note Independent of the console proxy
     */
    // @zh: 代理落定终结
    // @zh: return 三态 ResStatus
    // @zh: note 与控制台代理相互独立
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetFileLogProxy).msg(yomk::msgs::FileLogProxy::shared(yomk::payload::FileLogProxy{m_proxy})).status();
    }
    /**
     * @brief Query all file logger names of this type
     * @return Response carrying StringArray (lexicographic order); returns Invalid if uninitialized
     */
    // @zh: 查询该类型全部文件日志器名
    // @zh: return Response 携带 StringArray（字典序）；未初始化返回 Invalid
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("file")).request();
    }
    /**
     * @brief Query the specified file logger state
     * @return Response; returns Invalid if uninitialized
     * @note The two tables allow duplicate names, this query looks up exactly by type (file)
     */
    // @zh: 查询指定文件日志器状态
    // @zh: return Response；未初始化返回 Invalid
    // @zh: note 两表允许同名，本查询按类型（file）精确单查
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_file, "file"})).request();
    }
    /**
     * @brief Query the file global master switch state (6 levels + proxy)
     * @return Response; returns Invalid if uninitialized
     */
    // @zh: 查询文件全局主闸状态（6 级别 + proxy）
    // @zh: return Response；未初始化返回 Invalid
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerFileGlobalState).request();
    }
    /**
     * @brief Create file logger, terminates the chain (reads the dir + file accumulation)
     * @return Tri-state ResStatus; empty name/empty directory returns Invalid, existing same name returns Failed
     */
    // @zh: 创建文件日志器终结（读 dir + file 累积）
    // @zh: return 三态 ResStatus；空名/空目录返回 Invalid，同名已存在返回 Failed
    yomk::payload::ResStatus create()
    {
        return yomk::server().url(yomk::url::loggerCreateFileLogger).msg(yomk::msgs::LogFile::shared(yomk::payload::LogFile{m_file, m_dir})).status();
    }
    /**
     * @brief Flush the specified logger's buffer to disk
     * @return Tri-state ResStatus; unknown name returns Failed
     */
    // @zh: 将指定日志器的缓冲落盘
    // @zh: return 三态 ResStatus；未知名返回 Failed
    yomk::payload::ResStatus flush()
    {
        return yomk::server().url(yomk::url::loggerWriteFileLog).msg(yomk::msgs::String::shared(m_file)).status();
    }
    /**
     * @brief Remove the specified logger (does not delete the .log file on disk)
     * @return Tri-state ResStatus; unknown name returns Failed
     */
    // @zh: 删除指定日志器（不删除磁盘上的 .log 文件）
    // @zh: return 三态 ResStatus；未知名返回 Failed
    yomk::payload::ResStatus remove()
    {
        return yomk::server().url(yomk::url::loggerDeleteLogger).msg(yomk::msgs::String::shared(m_file)).status();
    }

private:
    YomkFileLogProxyFunc m_proxy;
};

/**
 * @brief Console management facet: after log_mgr().console(name);
 * the console has no emission path, emission goes through the log domain
 */
// @zh: 控制台管理分面：log_mgr().console(name) 之后
// @zh: 控制台无发射路径，发射走 log 域
class ConsoleLogBuilder
{
    std::string m_name;                // 空 = 全局主闸, 具名 = 逐器门控
    yomk::payload::LogLevel m_level = yomk::payload::LogLevel::Info;  // level() 累积, enable/disable 共用, 缺省 Info
    YomkConsoleLogProxyFunc m_proxy;
public:
    ConsoleLogBuilder(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_name(name)
    {
    }
    /**
     * @brief Accumulate gating level (shared by enable/disable)
     * @param lv Target level, default Info
     * @return Reference to self for chaining
     */
    // @zh: 累积门控级别（enable/disable 共用）
    // @zh: lv 目标级别，缺省 Info
    // @zh: return 自身链式引用
    ConsoleLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    /**
     * @brief Enable gating, terminates the chain
     * @return Tri-state ResStatus
     * @note Empty name = global master switch, named = per-logger gating
     */
    // @zh: 开启门控终结
    // @zh: return 三态 ResStatus
    // @zh: note name 空 = 全局主闸，具名 = 逐器门控
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(true, true, m_level, m_name);
    }
    /**
     * @brief Disable gating, terminates the chain
     * @return Tri-state ResStatus
     * @note Empty name = global master switch, named = per-logger gating
     */
    // @zh: 关闭门控终结
    // @zh: return 三态 ResStatus
    // @zh: note name 空 = 全局主闸，具名 = 逐器门控
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(true, false, m_level, m_name);
    }
    /**
     * @brief Accumulate console log proxy callback
     * @param fn Proxy callback, nullptr = restore default output
     * @return Reference to self for chaining
     */
    // @zh: 累积控制台日志代理回调
    // @zh: fn 代理回调，nullptr = 恢复默认输出
    // @zh: return 自身链式引用
    ConsoleLogBuilder& proxy(YomkConsoleLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    /**
     * @brief Proxy commit termination
     * @return Tri-state ResStatus
     */
    // @zh: 代理落定终结
    // @zh: return 三态 ResStatus
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetConsoleProxy).msg(yomk::msgs::ConsoleLogProxy::shared(yomk::payload::ConsoleLogProxy{m_proxy})).status();
    }
    /**
     * @brief Query all console logger names
     * @return Response carrying StringArray (lexicographic order); returns Invalid if uninitialized
     */
    // @zh: 查询全部控制台日志器名
    // @zh: return Response 携带 StringArray（字典序）；未初始化返回 Invalid
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("console")).request();
    }
    /**
     * @brief Query the specified console logger state
     * @return Response; returns Invalid if uninitialized
     * @note The two tables allow duplicate names, this query looks up exactly by type (console)
     */
    // @zh: 查询指定控制台日志器状态
    // @zh: return Response；未初始化返回 Invalid
    // @zh: note 两表允许同名，本查询按类型（console）精确单查
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_name, "console"})).request();
    }
    /**
     * @brief Query the console global master switch state (6 levels + proxy)
     * @return Response; returns Invalid if uninitialized
     */
    // @zh: 查询控制台全局主闸状态（6 级别 + proxy）
    // @zh: return Response；未初始化返回 Invalid
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loggerConsoleGlobalState).request();
    }
};

/**
 * @brief Location prefix switch facet: after log_mgr().location();
 * three switches accumulate (initial values carry the current process-level state), then apply() terminates
 */
// @zh: 位置前缀开关分面：log_mgr().location() 之后
// @zh: 三开关累积（初值承接当前进程级状态）→ apply() 终结
class LocSwitch
{
    bool m_file;   // 初值承接 YomkSourceLoc 当前状态(进程级开关)
    bool m_line;
    bool m_func;
public:
    LocSwitch()
        : m_file(YomkSourceLoc::fileFlag().load(std::memory_order_relaxed)),
          m_line(YomkSourceLoc::lineFlag().load(std::memory_order_relaxed)),
          m_func(YomkSourceLoc::funcFlag().load(std::memory_order_relaxed))
    {
    }
    /**
     * @brief Accumulate the file name display switch
     * @param on true = show
     * @return Reference to self for chaining
     */
    // @zh: 累积 file 名展示开关
    // @zh: on true = 显示
    // @zh: return 自身链式引用
    LocSwitch& show_file(bool on)
    {
        m_file = on;
        return *this;
    }
    /**
     * @brief Accumulate the line number display switch
     * @param on true = show
     * @return Reference to self for chaining
     */
    // @zh: 累积行号展示开关
    // @zh: on true = 显示
    // @zh: return 自身链式引用
    LocSwitch& show_line(bool on)
    {
        m_line = on;
        return *this;
    }
    /**
     * @brief Accumulate the function name display switch
     * @param on true = show
     * @return Reference to self for chaining
     */
    // @zh: 累积函数名展示开关
    // @zh: on true = 显示
    // @zh: return 自身链式引用
    LocSwitch& show_func(bool on)
    {
        m_func = on;
        return *this;
    }
    /**
     * @brief Location prefix commit termination (takes effect at process level, thread-safe, sets all three switches at once)
     * @return Always Succeeded
     */
    // @zh: 位置前缀落定终结（进程级生效，线程安全，一次性落三开关）
    // @zh: return 恒 Succeeded
    yomk::payload::ResStatus apply()
    {
        YomkSourceLoc::fileFlag().store(m_file);
        YomkSourceLoc::lineFlag().store(m_line);
        YomkSourceLoc::funcFlag().store(m_func);
        return yomk::payload::ResStatus::Succeeded;
    }
};

/**
 * @brief Log management domain builder: console/file facet entries + cross-facet accumulation (dir) + global introspection
 */
// @zh: 日志管理域 builder：console/file 分面入口 + 跨分面累积（dir）+ 全局内省
class LogMgrBuilder
{
    std::string m_dir;
public:
    /**
     * @brief Accumulate log directory (for create)
     * @param logDir Target directory
     * @return Reference to self for chaining
     * @pre Must be called before accumulating file()
     */
    // @zh: 累积日志目录（create 用）
    // @zh: logDir 目标目录
    // @zh: return 自身链式引用
    // @zh: pre 须先于 file() 累积调用
    LogMgrBuilder& dir(std::string_view logDir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_dir = logDir;
        return *this;
    }
    /**
     * @brief Switch into the file logger management facet
     * @param name Target logger name, empty = global master switch semantics (proxy/status/list do not use the name)
     * @return File management facet (carries the dir accumulation)
     */
    // @zh: 切入文件日志器管理分面
    // @zh: name 目标日志器名，空 = 全局主闸语义（proxy/status/list 不用名）
    // @zh: return 文件管理分面（携带 dir 累积）
    FileLogBuilder file(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return FileLogBuilder(name, m_dir);
    }
    /**
     * @brief Switch into the console management facet
     * @param name Target logger name, empty = global master switch
     * @return Console management facet
     */
    // @zh: 切入控制台管理分面
    // @zh: name 目标日志器名，空 = 全局主闸
    // @zh: return 控制台管理分面
    ConsoleLogBuilder console(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return ConsoleLogBuilder(name);
    }
    /**
     * @brief Switch into the location prefix switch facet
     * @return LocSwitch (initial values carry the current process-level switch state)
     */
    // @zh: 切入位置前缀开关分面
    // @zh: return LocSwitch（初值承接当前进程级开关状态）
    LocSwitch location()
    {
        return LocSwitch{};
    }
    /**
     * @brief Query all loggers with per-logger gating state
     * @return Response carrying LoggerInfoList (console/file entries hold their own per-logger gating layer truth);
     *     returns Invalid if uninitialized
     */
    // @zh: 查询全部日志器及逐器门控状态
    // @zh: return Response 携带 LoggerInfoList（console/file 条目为各自逐器门控层真值）；未初始化返回 Invalid
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
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
