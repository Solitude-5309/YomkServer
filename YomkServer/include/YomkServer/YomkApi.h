#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>

#include "YomkExport.h"
#include "YomkMsgs.h"
#include "YomkServer.h"
#include "YomkService.h"
#include "YomkDiag.h"
#include "YomkUrl.h"

// 启动编排接口：按 before -> start -> after 顺序执行
class YOMKSERVER_EXPORT YomkBoot
{
public:
    virtual ~YomkBoot() {}
    virtual int before() = 0;
    virtual int start() = 0;
    virtual int after() = 0;
};

// YomkServer 全局统一入口与便捷调用类
class YOMKSERVER_EXPORT YomkAPI
{
    // VERSION_API
public:
    // 获取框架版本号
    static std::string version();
    // BOOT_API
public:
    // 初始化框架单例并启动内置服务，asyncThreadCount 为异步请求线程池大小（0 表示自动检测）
    static void init(std::size_t asyncThreadCount = 0)
    {
        static std::once_flag initFlag;
        std::call_once(
            initFlag,
            [asyncThreadCount]()
            {
                auto server = YomkServer::create(asyncThreadCount);
                server->startService(
                    {"/YomkFunctionPool", "/YomkContext", "/YomkEventLoop", "/YomkLogger", "/YomkServerInfo"});
                setServer(server);
                std::atexit([]() { holder().destroy(); });
            });
    }
    // 获取全局单例，未初始化返回 nullptr
    static std::shared_ptr<YomkServer> serverInstance() { return serverSnapshot(); }
    // 关闭服务器并释放所有服务
    static void shutdown()
    {
        auto server = serverSnapshot();
        if (!server)
        {
            return;
        }
        server->shutdown();
        setServer(nullptr);
    }
    // 创建并注册指定类型的服务
    template <typename T>
    static int newService(const std::string& srvName = "")
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }
        return server->newService<T>(srvName);
    }
    // 注册已有服务实例（所有权移交给框架）
    static int addService(YomkService* srv = nullptr, const std::string& srvName = "")
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }

        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkService is null");
            return -1;
        }

        if (srvName != "")
            srv->name(srvName);

        return server->addService(srv);
    }
    // 按名称注销服务
    static int delService(const std::string& srvName = "")
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }

        if (srvName.empty())
        {
            YOMK_DIAG_ERROR("service name is empty");
            return -1;
        }

        return server->delService(srvName);
    }
    // 初始化并执行启动编排流程
    static int boot(YomkBoot* cfg = nullptr)
    {
        init();

        std::unique_ptr<YomkBoot> bootGuard(cfg);
        if (!bootGuard)
            return 0;

        int ret = 0;
        ret = bootGuard->before();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot before failed! ");
            return ret;
        }

        ret = bootGuard->start();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot start failed! ");
            return ret;
        }

        ret = bootGuard->after();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot after failed! ");
            return ret;
        }

        return 0;
    }
    // REQ_API
public:
    // 异步请求服务功能函数
    static void asyncRequest(const std::string& url, YomkMsgPtr pkg, YomkResponseFunc func)
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return;
        }
        return server->asyncRequest(url, pkg, func);
    }
    // 同步请求服务功能函数
    static yomk::payload::Response request(const std::string& url, YomkMsgPtr pkg)
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return server->request(url, pkg);
    }
    // LOG_API
private:
    // 调用点位置信息，仅模块内部使用。聚合体 + NSDMI：调用点 {} 初始化时在
    // 【调用行】完成 file/line/func 捕获；若改在函数体内求值，捕获到的是本头文件行号
    struct SourceLoc
    {
        const char* filename = __builtin_FILE();
        unsigned line = __builtin_LINE();
        const char* funcname = __builtin_FUNCTION();

        // 位置信息输出开关（进程级配置，atomic 保证线程安全）。必须为 public：
        // 外围类对嵌套类私有成员无特殊访问权（[class.access.nest]），YomkAPI 的
        // setter 需转发到这里；SourceLoc 整体为 private，该类型用户无法命名
        static std::atomic<bool>& fileFlag() { static std::atomic<bool> v(false); return v; }
        static std::atomic<bool>& lineFlag() { static std::atomic<bool> v(true); return v; } // 默认开（与旧宏输出兼容）
        static std::atomic<bool>& funcFlag() { static std::atomic<bool> v(false); return v; }

        // 取路径 basename（兼容 '/' 与 '\'），与主流日志风格一致
        static const char* baseName(const char* path)
        {
            const char* slash = std::strrchr(path, '/');
            const char* bslash = std::strrchr(path, '\\');
            const char* sep = slash > bslash ? slash : bslash;
            if (sep == nullptr)
            {
                return path;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            return sep + 1;
        }

        // 按开关将位置前缀 "[file:line:func]" 原地追加到 out；三项全关不追加。
        // 默认配置（仅行号）输出 "[123]"，与旧宏输出字节级一致
        void appendTo(std::string& out) const
        {
            const bool hasFile = fileFlag().load(std::memory_order_relaxed);
            const bool hasLine = lineFlag().load(std::memory_order_relaxed);
            const bool hasFunc = funcFlag().load(std::memory_order_relaxed);
            if (!hasFile && !hasLine && !hasFunc)
            {
                return; // 三项全关：无位置前缀
            }

            out += "[";
            bool first = true;
            auto append = [&out, &first](const std::string& part) {
                if (!first)
                {
                    out += ":";
                }
                out += part;
                first = false;
            };
            if (hasFile)
            {
                append(baseName(filename)); // 取 basename，主流日志风格
            }
            if (hasLine)
            {
                append(std::to_string(line));
            }
            if (hasFunc)
            {
                append(funcname);
            }
            out += "]";
        }
    };

    // emit 正文起始预留容量：典型日志单行 64 字节内，预分配减少 string 扩容
    static std::size_t logContentReserve() { static const std::size_t v = 64; return v; }

    // 逐参追加：每个实参前补一个空格，与旧 ostringstream 路径输出逐字节一致。
    // 常用类型直拼，整型手写 itoa，浮点 snprintf，其余可流输出类型回退 ostringstream。
    // bool 刻意输出 true/false（用户定版），不沿用 ostream 默认的 1/0
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

public:
    // 位置信息输出开关，转发至 SourceLoc 内部开关（进程级生效，线程安全）
    static void setLocFileEnabled(bool on) { SourceLoc::fileFlag().store(on); }
    static void setLocLineEnabled(bool on) { SourceLoc::lineFlag().store(on); }
    static void setLocFuncEnabled(bool on) { SourceLoc::funcFlag().store(on); }
    // 无 tag 日志宏的默认 tag 常量（仅宏内部使用）
    static const std::string& mainLoggerTag() { static const std::string t("MainLogger"); return t; }

public:
    // 设置控制台日志代理回调（传 nullptr 恢复默认输出，取消代理回调）
    static yomk::payload::ResStatus setConsoleLogProxy(YomkConsoleLogProxyFunc func)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerSetConsoleProxy(), yomk::msgs::ConsoleLogProxy::shared(yomk::payload::ConsoleLogProxy{func}));
    }
    // 设置文件日志代理回调（传 nullptr 恢复默认落盘，取消代理回调；与控制台代理相互独立）
    static yomk::payload::ResStatus setFileLogProxy(YomkFileLogProxyFunc func)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerSetFileLogProxy(), yomk::msgs::FileLogProxy::shared(yomk::payload::FileLogProxy{func}));
    }
    // 控制台日志输出
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogInfo(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogWarn(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogError(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogDebug(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogTrace(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogFatal(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerConsoleLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, std::move(content), tag, tag}));
    }
    // 创建文件日志器
    static yomk::payload::ResStatus createFileLogger(const std::string& logDir, const std::string& logFile)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerCreateFileLogger(), yomk::msgs::LogFile::shared(yomk::payload::LogFile{logFile, logDir}));
    }
    // 将指定日志器的缓冲落盘
    static yomk::payload::ResStatus flushFileLog(const std::string& logFile)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerWriteFileLog(), yomk::msgs::String::shared(logFile));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogInfo(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogWarn(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogError(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogDebug(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogTrace(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogFatal(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(YomkUrl::loggerFileLog(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, std::move(content), logFile, tag}));
    }
    // 开关各级别控制台日志（loggerName 空 = 全局主闸，与既有无参语义一致；具名 = 逐器门控）
    static yomk::payload::ResStatus enableConsoleLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, "", loggerName}));
    }
    static yomk::payload::ResStatus enableConsoleLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "", loggerName}));
    }
    static yomk::payload::ResStatus enableConsoleLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, "", loggerName}));
    }
    static yomk::payload::ResStatus enableConsoleLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, "", loggerName}));
    }
    static yomk::payload::ResStatus enableConsoleLogTrace(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, "", loggerName}));
    }
    static yomk::payload::ResStatus enableConsoleLogFatal(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogTrace(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, "", loggerName}));
    }
    static yomk::payload::ResStatus disableConsoleLogFatal(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffConsoleLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, "", loggerName}));
    }
    // 开关各级别文件日志（loggerName 空 = 文件全局主闸；具名 = 逐器门控，未知名返回 ResStatus::Failed）
    static yomk::payload::ResStatus enableFileLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, "", loggerName}));
    }
    static yomk::payload::ResStatus enableFileLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "", loggerName}));
    }
    static yomk::payload::ResStatus enableFileLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, "", loggerName}));
    }
    static yomk::payload::ResStatus enableFileLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Debug, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Info, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Warn, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Error, "", loggerName}));
    }
    static yomk::payload::ResStatus enableFileLogTrace(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, "", loggerName}));
    }
    static yomk::payload::ResStatus enableFileLogFatal(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOnFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogTrace(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Trace, "", loggerName}));
    }
    static yomk::payload::ResStatus disableFileLogFatal(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerOffFileLogByLevel(), yomk::msgs::Log::shared(yomk::payload::Log{yomk::payload::LogLevel::Fatal, "", loggerName}));
    }
    // 按类型获取日志器名列表（type: "console"/"file"，仅返回该类型的纯名）
    static yomk::payload::Response listLoggers(const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loggerLoggers(), yomk::msgs::String::shared(type));
    }
    // 查询指定日志器状态（type: "console"/"file"；两表允许同名，须按类型精确单查）
    static yomk::payload::Response getLoggerInfo(const std::string& loggerName, const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loggerLogger(), yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{loggerName, type}));
    }
    // 查询全部日志器及逐器门控状态（返回 LoggerInfoList 清单，每条内嵌 LoggerState 状态段：
    // console 条目为控制台逐器门控层真值，file 条目为文件逐器门控层真值）
    static yomk::payload::Response getAllLoggerInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loggerAll(), nullptr);
    }
    // 查询全局主闸状态（4 级别 + proxy；type: "console"/"file"，两套主闸相互独立供两层归因；
    // 缺省 "console" 与历史无参语义一致，未知 type 返回 ResStatus::Failed）
    static yomk::payload::Response getGlobalLoggerLevel(const std::string& type = "console")
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        if (type == "file")
        {
            return request(YomkUrl::loggerFileGlobalState(), nullptr);
        }
        if (type == "console")
        {
            return request(YomkUrl::loggerConsoleGlobalState(), nullptr);
        }
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type.");
    }
    // 删除指定日志器
    static yomk::payload::ResStatus deleteFileLogger(const std::string& loggerName)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loggerDeleteLogger(), yomk::msgs::String::shared(loggerName));
    }
    // CONTEXT_API
public:
    // 创建上下文键值（groupName 缺省 "main"；组名约定永远是 API 最后一个参数，下同）
    static yomk::payload::ResStatus createContext(
        const std::string& ctxName, YomkMsgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxCreate(), yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctx, groupName}));
    }
    // 获取上下文值（只读快照），改动请构造新对象再 setContext，勿原地修改（否则与并发读竞争、可能崩溃）
    template <typename T>
    static std::shared_ptr<T> getContext(const std::string& ctxName,
        std::shared_ptr<T> ctxDefault, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return ctxDefault;
        }
        yomk::payload::Response response =
            request(YomkUrl::ctxGet(), yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctxDefault, groupName}));
        if (response.m_status == yomk::payload::ResStatus::Succeeded)
        {
            std::shared_ptr<T> ctxData = nullptr;
            if (response.m_data && response.m_data->name() == T::kName)
            {
                ctxData = std::dynamic_pointer_cast<T>(response.m_data);
            }
            if (ctxData)
                return ctxData;
            else
                return ctxDefault;
        }
        else
        {
            YOMK_DIAG_ERROR("get context failed");
            return ctxDefault;
        }
    }
    // 设置上下文值（整体替换）
    static yomk::payload::ResStatus setContext(
        const std::string& ctxName, YomkMsgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxSet(), yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctx, groupName}));
    }
    // 开关上下文校验门控
    static yomk::payload::ResStatus enableContextChecker(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxTurnOnChecker(), yomk::msgs::String::shared(groupName));
    }
    static yomk::payload::ResStatus disableContextChecker(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxTurnOffChecker(), yomk::msgs::String::shared(groupName));
    }
    // 设置上下文校验门控回调
    static yomk::payload::ResStatus setContextChecker(
        const std::string& ctxName, YomkContextCheckerFunc checker, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::ctxSetChecker(), yomk::msgs::ContextChecker::shared(yomk::payload::ContextChecker{ctxName, checker, groupName}));
    }
    // 开关上下文变更监听器
    static yomk::payload::ResStatus enableContextMonitor(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxTurnOnMonitor(), yomk::msgs::String::shared(groupName));
    }
    static yomk::payload::ResStatus disableContextMonitor(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxTurnOffMonitor(), yomk::msgs::String::shared(groupName));
    }
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    static yomk::payload::ResStatus setContextMonitor(const std::string& ctxName, YomkContextMonitorFunc monitor,
        bool async = false, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::ctxSetMonitor(),
            yomk::msgs::ContextMonitor::shared(yomk::payload::ContextMonitor{ctxName, monitor, async, groupName}));
    }
    // 销毁指定上下文键
    static yomk::payload::ResStatus destroyContext(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::ctxDestroy(), yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{groupName, ctxName}));
    }
    // 获取全部上下文键名
    static yomk::payload::Response listContextKeys(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::ctxKeys(), yomk::msgs::String::shared(groupName));
    }
    // 查询指定上下文键状态
    static yomk::payload::Response getContextInfo(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::ctxKey(), yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{groupName, ctxName}));
    }
    // 查询全部组名清单（返回 StringArray，对齐 listEventLoops 形态）
    static yomk::payload::Response listContextGroups()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::ctxGroups(), nullptr);
    }
    // 查询全部组及其全部键的内省元信息（跨组聚合：返回 ContextGroupInfoList，层级为组 → 键元信息）
    static yomk::payload::Response getAllContextInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::ctxAll(), nullptr);
    }
    // EVENTLOOP_API
public:
    // 启动指定事件循环
    static yomk::payload::ResStatus startEventLoop(
        const std::string& eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string& msgName = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::loopStart(),
            yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环线程（保留未执行事件）
    static yomk::payload::ResStatus stopEventLoop(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loopStop(), yomk::msgs::String::shared(eventLoopName));
    }
    // 异步投递事件
    static yomk::payload::ResStatus postEvent(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::loopPost(), yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（阻塞等待执行完成）
    static yomk::payload::ResStatus postEventAndWait(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::loopPostWait(), yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环并清空队列
    static yomk::payload::ResStatus destroyEventLoop(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::loopDestroy(), yomk::msgs::String::shared(eventLoopName));
    }
    // 获取全部事件循环名称列表
    static yomk::payload::Response listEventLoops()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loopLoops(), nullptr);
    }
    // 查询指定事件循环状态与事件积压数
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loopLoop(), yomk::msgs::String::shared(eventLoopName));
    }
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName, size_t tagCount)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loopLoop(), yomk::msgs::String::shared(eventLoopName + " " + std::to_string(tagCount)));
    }
    // 查询全部事件循环状态
    static yomk::payload::Response getAllEventLoopInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::loopAll(), nullptr);
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池（msgName 声明期望消息类型；description 函数说明，内容使用者自定义，均可为空）
    static yomk::payload::ResStatus registerFunction(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
        const std::string& description = "")
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(
            YomkUrl::fpRegister(), yomk::msgs::Function::shared(yomk::payload::Function{funcName, func, msgName, description}));
    }
    // 从函数池注销函数
    static yomk::payload::ResStatus unregisterFunction(const std::string& funcName)
    {
        if (!requireServer())
        {
            return yomk::payload::ResStatus::Invalid;
        }
        return requestStatus(YomkUrl::fpUnregister(), yomk::msgs::String::shared(funcName));
    }
    // 调用函数池中注册的函数
    static yomk::payload::Response callFunction(const std::string& funcName, YomkMsgPtr callData)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::fpCall(), yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{funcName, callData}));
    }
    // 获取函数池中全部函数名
    static yomk::payload::Response listFunctionNames()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::fpNames(), nullptr);
    }
    // 查询单个已注册函数元信息
    static yomk::payload::Response getFunctionInfo(const std::string& funcName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::fpName(), yomk::msgs::String::shared(funcName));
    }
    // 查询全部已注册函数元信息
    static yomk::payload::Response getAllFunctionInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::fpAll(), nullptr);
    }
    // SERVER_INFO_API
public:
    // 查询所有服务名称
    static yomk::payload::Response listServices()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::srvServices(), nullptr);
    }
    // 查询指定服务的全部功能函数（返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，
    // 与 /function 单查、/all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    static yomk::payload::Response listServiceFunctions(const std::string& srvName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::srvFunctions(), yomk::msgs::String::shared(srvName));
    }
    // 查询指定功能函数元信息
    static yomk::payload::Response getServiceFunctionInfoByUrl(const std::string& url)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::srvFunction(), yomk::msgs::String::shared(url));
    }
    // 查询框架全量服务及功能函数拓扑
    static yomk::payload::Response getAllServerInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(YomkUrl::srvAll(), nullptr);
    }

private:
    // 全局版本号：server 注入与置空统一经 setServer，写后 fetch_add(1, release)
    // 失效所有线程的 thread_local 缓存
    static std::atomic<uint64_t>& serverEpoch()
    {
        static std::atomic<uint64_t> e(0);
        return e;
    }
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        // 每线程缓存快照，热路径仅 1 次 epoch acquire load + 比对；慢路径仅首调与
        // 失效后进入。!cached 使空快照永不作为有效命中：未初始化期每次调用仍走慢
        // 路径，契约消息语义不变。并发 shutdown 的陈旧窗口内至多一调用命中缓存
        // 保活的 server，返回服务级失败，无悬垂
        thread_local std::shared_ptr<YomkServer> cached;
        thread_local uint64_t cachedEpoch = 0;
        const uint64_t e = serverEpoch().load(std::memory_order_acquire);
        if (cachedEpoch != e || !cached)
        {
            std::lock_guard<std::mutex> lock(mtx());
            cached = holder().server;
            cachedEpoch = e;
        }
        return cached;
    }
    // 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值
    static bool requireServer()
    {
        if (serverSnapshot())
            return true;
        YOMK_DIAG_ERROR("YomkServer is not init");
        return false;
    }
    // 同步请求并收敛为三态：失败原因经错误日志承载，返回值仅保留 ResStatus
    static yomk::payload::ResStatus requestStatus(const std::string& url, YomkMsgPtr pkg)
    {
        yomk::payload::Response resp = request(url, std::move(pkg));
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.m_msg.empty() ? "request failed" : resp.m_msg));
        }
        return resp.m_status;
    }
    static void setServer(std::shared_ptr<YomkServer> server)
    {
        {
            std::lock_guard<std::mutex> lock(mtx());
            holder().server = std::move(server);
        }
        serverEpoch().fetch_add(1, std::memory_order_release); // 锁外失效全部线程缓存
    }
    struct ServerHolder
    {
        void destroy()
        {
            std::shared_ptr<YomkServer> local;
            {
                std::lock_guard<std::mutex> lock(YomkAPI::mtx());
                local = std::move(YomkAPI::holder().server);
            }
            local.reset();
        }
        std::shared_ptr<YomkServer> server;
    };
    static std::mutex& mtx()
    {
        static std::mutex* const m = new std::mutex;
        return *m;
    }
    static ServerHolder& holder()
    {
        static ServerHolder* const h = new ServerHolder;
        return *h;
    }
};
// 日志宏（正式 API）：在调用行展开，注入 {} 于该行完成位置捕获（file/line/func，
// 默认仅行号 [N]，见 setLoc*Enabled 开关）。位置参数由宏注入，用户不写。
#define YOMK_INFO(...) YomkAPI::emitConsoleLogInfo(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_INFO_TAG(tag, ...) YomkAPI::emitConsoleLogInfo(tag, {}, __VA_ARGS__)
#define YOMK_WARN(...) YomkAPI::emitConsoleLogWarn(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_WARN_TAG(tag, ...) YomkAPI::emitConsoleLogWarn(tag, {}, __VA_ARGS__)
#define YOMK_ERROR(...) YomkAPI::emitConsoleLogError(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_ERROR_TAG(tag, ...) YomkAPI::emitConsoleLogError(tag, {}, __VA_ARGS__)
#define YOMK_DEBUG(...) YomkAPI::emitConsoleLogDebug(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_DEBUG_TAG(tag, ...) YomkAPI::emitConsoleLogDebug(tag, {}, __VA_ARGS__)
#define YOMK_TRACE(...) YomkAPI::emitConsoleLogTrace(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_TRACE_TAG(tag, ...) YomkAPI::emitConsoleLogTrace(tag, {}, __VA_ARGS__)
#define YOMK_FATAL(...) YomkAPI::emitConsoleLogFatal(YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FATAL_TAG(tag, ...) YomkAPI::emitConsoleLogFatal(tag, {}, __VA_ARGS__)
#define YOMK_FILE_INFO(file, ...) \
    YomkAPI::emitFileLogInfo(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_INFO_TAG(file, tag, ...) \
    YomkAPI::emitFileLogInfo(file, tag, {}, __VA_ARGS__)
#define YOMK_FILE_WARN(file, ...) \
    YomkAPI::emitFileLogWarn(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_WARN_TAG(file, tag, ...) \
    YomkAPI::emitFileLogWarn(file, tag, {}, __VA_ARGS__)
#define YOMK_FILE_ERROR(file, ...) \
    YomkAPI::emitFileLogError(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_ERROR_TAG(file, tag, ...) \
    YomkAPI::emitFileLogError(file, tag, {}, __VA_ARGS__)
#define YOMK_FILE_DEBUG(file, ...) \
    YomkAPI::emitFileLogDebug(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_DEBUG_TAG(file, tag, ...) \
    YomkAPI::emitFileLogDebug(file, tag, {}, __VA_ARGS__)
#define YOMK_FILE_TRACE(file, ...) \
    YomkAPI::emitFileLogTrace(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_TRACE_TAG(file, tag, ...) \
    YomkAPI::emitFileLogTrace(file, tag, {}, __VA_ARGS__)
#define YOMK_FILE_FATAL(file, ...) \
    YomkAPI::emitFileLogFatal(file, YomkAPI::mainLoggerTag(), {}, __VA_ARGS__)
#define YOMK_FILE_FATAL_TAG(file, tag, ...) \
    YomkAPI::emitFileLogFatal(file, tag, {}, __VA_ARGS__)
