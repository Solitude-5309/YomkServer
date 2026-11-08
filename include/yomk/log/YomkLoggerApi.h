#pragma once
#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "yomk/log/YomkLoggerMsgs.h"
#include "yomk/log/YomkLoggerPayload.h"
#include "yomk/log/YomkLoggerUrl.h"
#include "yomk/YomkStdMsgs.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerPayload.h"
#include "yomk/YomkSourceLoc.h"

// 日志域：发射（yomk::log()）与管理（yomk::log_mgr()）分域。发射链经请求域 .status()
// 三态收敛; 管理操作无发射语义, 入口不携带 YomkSourceLoc。
// 调用行位置捕获由域入口默认实参承担（__builtin_FILE/LINE/FUNCTION 在默认实参表达式
// 中求值时捕获调用方位置）。
/**
 * @brief 日志链式 Builder，发射与门控/内省/生命周期管理分域
 *
 * @par 主控制台发射
 * yomk::log().info(...) 等 ×6（info / warn / error / debug / trace / fatal）
 * @li 返回三态 ResStatus; 未初始化返回 Invalid
 * @li 位置前缀在调用行捕获（默认仅行号 [N], 开关见"位置前缀"）
 * @li tag 缺省 main, .tag(t) 累积更换
 * @li 参数逐个追加（模板 pack, 无上限）, bool 输出 true/false
 *
 * @par 具名文件发射
 * yomk::log().to_sink(name).info(...) 等 ×6
 * @li .tag(t) 可叠加; to_sink 分面携带调用行位置
 *
 * @par 门控（管理域）
 * yomk::log_mgr().console(name).level(lv).enable() / .disable()
 * yomk::log_mgr().file(name).level(lv).enable() / .disable()
 * @li name 省略/空 = 全局主闸, 具名 = 逐器门控（文件路径未知名返回 Failed）
 * @li .level(lv) 缺省 Info
 *
 * @par 代理与位置前缀（管理域）
 * @li yomk::log_mgr().console().proxy(fn).apply(): 控制台代理, nullptr 恢复默认
 * @li yomk::log_mgr().file().proxy(fn).apply(): 文件代理, nullptr 恢复默认落盘
 * @li yomk::log_mgr().location().show_file(on).show_line(on).show_func(on).apply():
 *     位置前缀三开关（进程级, 一次性落三开关）
 *
 * @par 文件日志器生命周期（管理域）
 * @li yomk::log_mgr().dir(dir).file(name).create(): 创建文件日志器
 * @li yomk::log_mgr().file(name).flush(): 缓冲落盘
 * @li yomk::log_mgr().file(name).remove(): 删除日志器
 *
 * @par 内省（管理域）
 * @li yomk::log_mgr().console().list() / yomk::log_mgr().file().list():
 *     该类型全部日志器名（StringArray, 字典序）
 * @li yomk::log_mgr().console(name).get_info() / yomk::log_mgr().file(name).get_info():
 *     指定日志器状态（两表允许同名, 须按类型精确单查）
 * @li yomk::log_mgr().console().status() / yomk::log_mgr().file().status():
 *     全局主闸状态（4 级别 + proxy, 两套主闸相互独立）
 * @li yomk::log_mgr().all(): 全部日志器及逐器门控状态（LoggerInfoList）
 *
 * @warning log() 域入口在调用行捕获位置, 仅限单表达式内联使用, 禁止缓存复用 Builder!
 */
// 公开面仅暴露域入口, builder/分面类机制内敛, 调用方全程以 auto 接收、从不具名引用类型
namespace yomk::detail
{
class LogBuilder;      // 前置声明, 定义见下方机制段
class LogMgrBuilder;
}
namespace yomk
{
/**
 * @brief 日志发射域入口：主控制台 / 具名文件发射
 * @note 位置在调用行捕获, 须单表达式内联使用
 * @return LogBuilder（详见类文档）
 */
inline detail::LogBuilder log(const YomkSourceLoc& loc = YomkSourceLoc{});  // 按值返回需完整类型, 定义在机制段之后
/**
 * @brief 日志管理域入口：门控 / 代理 / 位置前缀 / 文件日志器生命周期 / 内省
 * @return LogMgrBuilder（详见类文档）
 */
inline detail::LogMgrBuilder log_mgr();
}

// —— 机制段: builder 与分面 ——
namespace yomk::detail
{
/**
 * @brief 日志正文格式化：起始预留容量 + 逐参追加的单一实现（原 YomkLogFmt.h 并入本头）
 * @note 输出定版：实参间以单个空格分隔, bool 输出 true/false, 整型/浮点与 ostream 默认一致;
 *     与链式表面无耦合, 外部自行拼接日志 content 亦可复用
 */
struct YomkLogFmt
{
    // emit 正文起始预留容量：典型日志单行 64 字节内，预分配减少 string 扩容
    static std::size_t logContentReserve() { static const std::size_t v = 64; return v; }

    // 逐参追加：每个实参前补一个空格。
    // 常用类型直拼, 整型手写 itoa, 浮点 snprintf, 其余可流输出类型回退 ostringstream。
    // bool 刻意输出 true/false（用户定版）, 不沿用 ostream 默认的 1/0
    static void appendLogArg(std::string& out, const char* v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, const std::string& v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, char v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, bool v) { out += ' '; out += (v ? "true" : "false"); }
    template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // 手写 itoa：负号 + 十进制数字，与 ostream 默认输出一致。负数 magnitude 在
        // 无符号域取 0 - raw（对最小值取负是 UB）；符号判定 is_signed 编译期短路，
        // 无符号类型不做回读比较
        using U = typename std::make_unsigned<T>::type;
        const U raw = static_cast<U>(v);
        const bool neg = std::is_signed<T>::value &&
                         static_cast<typename std::make_signed<U>::type>(raw) < 0;
        U u = neg ? static_cast<U>(0) - raw : raw;
        constexpr std::size_t kDigitBufSize = 24; // uint64 十进制至多 20 位，24 含余量
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
        // %g（6 位有效数字）与 ostream 默认输出一致；long double 需 %Lg。
        // 栈缓冲 64 字节足够（最长形如 -1.23457e-308）
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
    // 回退重载：非算术、且不可转 const char*/std::string 的任意可流输出类型
    // （enable_if 条件与非模板重载集互斥）
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
// 逐级别开关公共核心: console/file × on/off × 六级别共用。
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
 * @brief 具名文件发射分面：log().to_sink(name) 之后继续累积并以级别方法终结
 */
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
     * @brief 累积日志 tag
     * @param t tag 名, 缺省 main
     * @return 自身链式引用
     */
    SinkLogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    // —— 发射终端 ×6（模板 pack 逐参追加; 位置前缀来自 log() 调用行捕获;
    //    返回三态 ResStatus, 阻塞至日志写入完成; 未初始化返回 Invalid）——
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

/**
 * @brief 发射域 builder：主控制台发射本体（不经 to_sink 分面即主控制台路径）
 */
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
     * @brief 累积日志 tag
     * @param t tag 名, 缺省 main
     * @return 自身链式引用
     */
    LogBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    /**
     * @brief 切入具名文件发射分面
     * @param name 目标文件日志器名
     * @return 文件发射分面（携带本调用行位置）
     * @warning 未知名写入返回 Failed, 不惰性创建
     */
    SinkLogBuilder to_sink(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return SinkLogBuilder(m_loc, name);
    }
    // —— 主控制台发射终端 ×6（模板 pack 逐参追加; 返回三态 ResStatus,
    //    阻塞至日志写入完成; 未初始化返回 Invalid）——
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

/**
 * @brief 文件日志器管理分面：log_mgr().file(name) 之后
 * 承载门控 / 代理 / 内省 / 生命周期终结
 */
class FileLogBuilder
{
    std::string m_file;
    std::string m_dir;                 // dir() 累积经 LogMgrBuilder::file() 携带（create 用）
    yomk::payload::LogLevel m_level = yomk::payload::LogLevel::Info;  // level() 累积, enable/disable 共用, 缺省 Info
public:
    FileLogBuilder(std::string_view file, std::string_view dir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_file(file), m_dir(dir)
    {
    }
    /**
     * @brief 累积门控级别（enable/disable 共用）
     * @param lv 目标级别, 缺省 Info
     * @return 自身链式引用
     */
    FileLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    /**
     * @brief 开启门控终结
     * @return 三态 ResStatus; 文件路径未知名返回 Failed
     * @note name 空 = 文件全局主闸, 具名 = 逐器门控
     */
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(false, true, m_level, m_file);
    }
    /**
     * @brief 关闭门控终结
     * @return 三态 ResStatus; 文件路径未知名返回 Failed
     * @note name 空 = 文件全局主闸, 具名 = 逐器门控
     */
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(false, false, m_level, m_file);
    }
    /**
     * @brief 累积文件日志代理回调
     * @param fn 代理回调, nullptr = 恢复默认落盘
     * @return 自身链式引用
     */
    FileLogBuilder& proxy(YomkFileLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    /**
     * @brief 代理落定终结
     * @return 三态 ResStatus
     * @note 与控制台代理相互独立
     */
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetFileLogProxy).msg(yomk::msgs::FileLogProxy::shared(yomk::payload::FileLogProxy{m_proxy})).status();
    }
    /**
     * @brief 查询该类型全部文件日志器名
     * @return Response 携带 StringArray（字典序）; 未初始化返回 Invalid
     */
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("file")).request();
    }
    /**
     * @brief 查询指定文件日志器状态
     * @return Response; 未初始化返回 Invalid
     * @note 两表允许同名, 本查询按类型（file）精确单查
     */
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_file, "file"})).request();
    }
    /**
     * @brief 查询文件全局主闸状态（4 级别 + proxy）
     * @return Response; 未初始化返回 Invalid
     */
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerFileGlobalState).request();
    }
    /**
     * @brief 创建文件日志器终结（读 dir + file 累积）
     * @return 三态 ResStatus; 空名/空目录返回 Invalid
     */
    yomk::payload::ResStatus create()
    {
        return yomk::server().url(yomk::url::loggerCreateFileLogger).msg(yomk::msgs::LogFile::shared(yomk::payload::LogFile{m_file, m_dir})).status();
    }
    /**
     * @brief 将指定日志器的缓冲落盘
     * @return 三态 ResStatus; 未知名返回 Failed
     */
    yomk::payload::ResStatus flush()
    {
        return yomk::server().url(yomk::url::loggerWriteFileLog).msg(yomk::msgs::String::shared(m_file)).status();
    }
    /**
     * @brief 删除指定日志器（不删除磁盘上的 .log 文件）
     * @return 三态 ResStatus; 未知名返回 Failed
     */
    yomk::payload::ResStatus remove()
    {
        return yomk::server().url(yomk::url::loggerDeleteLogger).msg(yomk::msgs::String::shared(m_file)).status();
    }

private:
    YomkFileLogProxyFunc m_proxy;
};

/**
 * @brief 控制台管理分面：log_mgr().console(name) 之后
 * 控制台无发射路径, 发射走 log 域
 */
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
     * @brief 累积门控级别（enable/disable 共用）
     * @param lv 目标级别, 缺省 Info
     * @return 自身链式引用
     */
    ConsoleLogBuilder& level(yomk::payload::LogLevel lv)
    {
        m_level = lv;
        return *this;
    }
    /**
     * @brief 开启门控终结
     * @return 三态 ResStatus
     * @note name 空 = 全局主闸, 具名 = 逐器门控
     */
    yomk::payload::ResStatus enable()
    {
        return setLogByLevel(true, true, m_level, m_name);
    }
    /**
     * @brief 关闭门控终结
     * @return 三态 ResStatus
     * @note name 空 = 全局主闸, 具名 = 逐器门控
     */
    yomk::payload::ResStatus disable()
    {
        return setLogByLevel(true, false, m_level, m_name);
    }
    /**
     * @brief 累积控制台日志代理回调
     * @param fn 代理回调, nullptr = 恢复默认输出
     * @return 自身链式引用
     */
    ConsoleLogBuilder& proxy(YomkConsoleLogProxyFunc fn)
    {
        m_proxy = std::move(fn);
        return *this;
    }
    /**
     * @brief 代理落定终结
     * @return 三态 ResStatus
     */
    yomk::payload::ResStatus apply()
    {
        return yomk::server().url(yomk::url::loggerSetConsoleProxy).msg(yomk::msgs::ConsoleLogProxy::shared(yomk::payload::ConsoleLogProxy{m_proxy})).status();
    }
    /**
     * @brief 查询全部控制台日志器名
     * @return Response 携带 StringArray（字典序）; 未初始化返回 Invalid
     */
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLoggers).msg(yomk::msgs::String::shared("console")).request();
    }
    /**
     * @brief 查询指定控制台日志器状态
     * @return Response; 未初始化返回 Invalid
     * @note 两表允许同名, 本查询按类型（console）精确单查
     */
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerLogger).msg(yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{m_name, "console"})).request();
    }
    /**
     * @brief 查询控制台全局主闸状态（4 级别 + proxy）
     * @return Response; 未初始化返回 Invalid
     */
    yomk::payload::Response status()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loggerConsoleGlobalState).request();
    }
};

/**
 * @brief 位置前缀开关分面：log_mgr().location() 之后
 * 三开关累积（初值承接当前进程级状态）→ apply() 终结
 */
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
    /**
     * @brief 累积 file 名展示开关
     * @param on true = 显示
     * @return 自身链式引用
     */
    LocSwitch& show_file(bool on)
    {
        m_file = on;
        return *this;
    }
    /**
     * @brief 累积行号展示开关
     * @param on true = 显示
     * @return 自身链式引用
     */
    LocSwitch& show_line(bool on)
    {
        m_line = on;
        return *this;
    }
    /**
     * @brief 累积函数名展示开关
     * @param on true = 显示
     * @return 自身链式引用
     */
    LocSwitch& show_func(bool on)
    {
        m_func = on;
        return *this;
    }
    /**
     * @brief 位置前缀落定终结（进程级生效, 线程安全, 一次性落三开关）
     * @return 恒 Succeeded
     */
    yomk::payload::ResStatus apply()
    {
        YomkSourceLoc::fileFlag().store(m_file);
        YomkSourceLoc::lineFlag().store(m_line);
        YomkSourceLoc::funcFlag().store(m_func);
        return yomk::payload::ResStatus::Succeeded;
    }
};

/**
 * @brief 日志管理域 builder：console/file 分面入口 + 跨分面累积（dir）+ 全局内省
 */
class LogMgrBuilder
{
    std::string m_dir;
public:
    /**
     * @brief 累积日志目录（create 用）
     * @param logDir 目标目录
     * @return 自身链式引用
     * @pre 须先于 file() 累积调用
     */
    LogMgrBuilder& dir(std::string_view logDir)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_dir = logDir;
        return *this;
    }
    /**
     * @brief 切入文件日志器管理分面
     * @param name 目标日志器名, 空 = 全局主闸语义（proxy/status/list 不用名）
     * @return 文件管理分面（携带 dir 累积）
     */
    FileLogBuilder file(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return FileLogBuilder(name, m_dir);
    }
    /**
     * @brief 切入控制台管理分面
     * @param name 目标日志器名, 空 = 全局主闸
     * @return 控制台管理分面
     */
    ConsoleLogBuilder console(std::string_view name = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        return ConsoleLogBuilder(name);
    }
    /**
     * @brief 切入位置前缀开关分面
     * @return LocSwitch（初值承接当前进程级开关状态）
     */
    LocSwitch location()
    {
        return LocSwitch{};
    }
    /**
     * @brief 查询全部日志器及逐器门控状态
     * @return Response 携带 LoggerInfoList（console/file 条目为各自逐器门控层真值）;
     *     未初始化返回 Invalid
     */
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
