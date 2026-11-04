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
#include "YomkPkg.h"
#include "YomkServer.h"
#include "YomkService.h"

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
            YOMK_ERR_POS_LOG("YomkServer is not init");
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
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return -1;
        }

        if (!srv)
        {
            YOMK_ERR_POS_LOG("YomkService is null");
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
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return -1;
        }

        if (srvName.empty())
        {
            YOMK_ERR_POS_LOG("service name is empty");
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
            YOMK_ERR_POS_LOG("YomkBoot before failed! ");
            return ret;
        }

        ret = bootGuard->start();
        if (ret != 0)
        {
            YOMK_ERR_POS_LOG("YomkBoot start failed! ");
            return ret;
        }

        ret = bootGuard->after();
        if (ret != 0)
        {
            YOMK_ERR_POS_LOG("YomkBoot after failed! ");
            return ret;
        }

        return 0;
    }
    // REQ_API
public:
    // 内置服务 URL 常量池：公开供绕过 API 语义函数、直接 request 的用户复用
    // （一切皆请求——服务写完 request API 自动就有，URL 即公共契约）。
    // 函数级 static 惰性构造一次、全程复用（C++11 静态初始化线程安全，
    // 与 mtx()/holder() 同一惯用法），消除每次 request 调用的 URL 临时 std::string 堆分配
    // （23 字符超 SSO 15 字符上限，实测 7.4→1.1 ns/调用）。访问器名 = 服务前缀
    // （logger/ctx/loop/fp/srv）+ 路由段驼峰化；"all" 等跨服务重名路由靠前缀区分。
    // server 层对 URL 仅做解析查找、不持有引用，静态生命周期无析构顺序风险
    struct Url
    {
        // /YomkLogger/*
        static const std::string& loggerSetConsoleProxy() { static const std::string u("/YomkLogger/set_console_log_proxy"); return u; }
        static const std::string& loggerSetFileLogProxy() { static const std::string u("/YomkLogger/set_file_log_proxy"); return u; }
        static const std::string& loggerConsoleLog() { static const std::string u("/YomkLogger/console_log"); return u; }
        static const std::string& loggerCreateFileLogger() { static const std::string u("/YomkLogger/create_file_logger"); return u; }
        static const std::string& loggerFileLog() { static const std::string u("/YomkLogger/file_log"); return u; }
        static const std::string& loggerWriteFileLog() { static const std::string u("/YomkLogger/write_file_log"); return u; }
        static const std::string& loggerDeleteLogger() { static const std::string u("/YomkLogger/delete_logger"); return u; }
        static const std::string& loggerOnConsoleLogByLevel() { static const std::string u("/YomkLogger/on_console_log_by_level"); return u; }
        static const std::string& loggerOffConsoleLogByLevel() { static const std::string u("/YomkLogger/off_console_log_by_level"); return u; }
        static const std::string& loggerOnFileLogByLevel() { static const std::string u("/YomkLogger/on_file_log_by_level"); return u; }
        static const std::string& loggerOffFileLogByLevel() { static const std::string u("/YomkLogger/off_file_log_by_level"); return u; }
        static const std::string& loggerLoggers() { static const std::string u("/YomkLogger/loggers"); return u; }
        static const std::string& loggerLogger() { static const std::string u("/YomkLogger/logger"); return u; }
        static const std::string& loggerAll() { static const std::string u("/YomkLogger/all"); return u; }
        static const std::string& loggerFileGlobalState() { static const std::string u("/YomkLogger/file_global_state"); return u; }
        static const std::string& loggerConsoleGlobalState() { static const std::string u("/YomkLogger/console_global_state"); return u; }
        // /YomkContext/*
        static const std::string& ctxCreate() { static const std::string u("/YomkContext/create"); return u; }
        static const std::string& ctxGet() { static const std::string u("/YomkContext/get"); return u; }
        static const std::string& ctxSet() { static const std::string u("/YomkContext/set"); return u; }
        static const std::string& ctxTurnOnChecker() { static const std::string u("/YomkContext/turn_on_checker"); return u; }
        static const std::string& ctxTurnOffChecker() { static const std::string u("/YomkContext/turn_off_checker"); return u; }
        static const std::string& ctxSetChecker() { static const std::string u("/YomkContext/set_checker"); return u; }
        static const std::string& ctxTurnOnMonitor() { static const std::string u("/YomkContext/turn_on_monitor"); return u; }
        static const std::string& ctxTurnOffMonitor() { static const std::string u("/YomkContext/turn_off_monitor"); return u; }
        static const std::string& ctxSetMonitor() { static const std::string u("/YomkContext/set_monitor"); return u; }
        static const std::string& ctxDestroy() { static const std::string u("/YomkContext/destroy"); return u; }
        static const std::string& ctxKeys() { static const std::string u("/YomkContext/keys"); return u; }
        static const std::string& ctxKey() { static const std::string u("/YomkContext/key"); return u; }
        static const std::string& ctxGroups() { static const std::string u("/YomkContext/groups"); return u; }
        static const std::string& ctxAll() { static const std::string u("/YomkContext/all"); return u; }
        // /YomkEventLoop/*
        static const std::string& loopStart() { static const std::string u("/YomkEventLoop/start"); return u; }
        static const std::string& loopStop() { static const std::string u("/YomkEventLoop/stop"); return u; }
        static const std::string& loopPost() { static const std::string u("/YomkEventLoop/post"); return u; }
        static const std::string& loopPostWait() { static const std::string u("/YomkEventLoop/post_wait"); return u; }
        static const std::string& loopDestroy() { static const std::string u("/YomkEventLoop/destroy"); return u; }
        static const std::string& loopLoops() { static const std::string u("/YomkEventLoop/loops"); return u; }
        static const std::string& loopLoop() { static const std::string u("/YomkEventLoop/loop"); return u; }
        static const std::string& loopAll() { static const std::string u("/YomkEventLoop/all"); return u; }
        // /YomkFunctionPool/*
        static const std::string& fpRegister() { static const std::string u("/YomkFunctionPool/register"); return u; }
        static const std::string& fpUnregister() { static const std::string u("/YomkFunctionPool/unregister"); return u; }
        static const std::string& fpCall() { static const std::string u("/YomkFunctionPool/call"); return u; }
        static const std::string& fpNames() { static const std::string u("/YomkFunctionPool/names"); return u; }
        static const std::string& fpName() { static const std::string u("/YomkFunctionPool/name"); return u; }
        static const std::string& fpAll() { static const std::string u("/YomkFunctionPool/all"); return u; }
        // /YomkServerInfo/*
        static const std::string& srvServices() { static const std::string u("/YomkServerInfo/services"); return u; }
        static const std::string& srvFunctions() { static const std::string u("/YomkServerInfo/functions"); return u; }
        static const std::string& srvFunction() { static const std::string u("/YomkServerInfo/function"); return u; }
        static const std::string& srvAll() { static const std::string u("/YomkServerInfo/all"); return u; }
    };
    // 异步请求服务功能函数
    static void asyncRequest(const std::string& url, YomkPkgPtr pkg, YomkResponseFunc func)
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return;
        }
        return server->asyncRequest(url, pkg, func);
    }
    // 同步请求服务功能函数
    static yomk::Response request(const std::string& url, YomkPkgPtr pkg)
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return server->request(url, pkg);
    }
    // LOG_API
private:
    // 调用点位置信息 + 渲染策略，仅模块内部使用，不对外暴露。
    // 聚合体 + NSDMI（成员默认初始化器），无用户构造函数——调用点 {}
    // 聚合初始化时 NSDMI 在【调用行】求值完成 file/line/func 捕获；
    // 若在函数体内调用 __builtin_LINE()，捕获到的将是本头文件内部行号
    struct SourceLoc
    {
        const char* filename = __builtin_FILE();
        unsigned line = __builtin_LINE();
        const char* funcname = __builtin_FUNCTION();

        // 位置信息输出开关（进程级配置，atomic 保证线程安全）。
        // 存储形态：静态成员函数 + 局部静态（Meyer's singleton 惯用法）——
        // C++11 兼容且 header-only 无需类外定义；刻意不用 C++17 inline 变量，
        // 使本模块新增代码整体保持 C++11 可编译。
        // 注意：必须为 public——C++ 规定外围类对嵌套类的私有成员没有特殊
        // 访问权（[class.access.nest]），YomkAPI 的公开 setter 需转发到这里；
        // SourceLoc 整体为 private，用户无法命名该类型，公开成员不会外泄
        static std::atomic<bool>& fileFlag() { static std::atomic<bool> v(false); return v; }
        static std::atomic<bool>& lineFlag() { static std::atomic<bool> v(true); return v; } // 默认开（与旧宏输出兼容）
        static std::atomic<bool>& funcFlag() { static std::atomic<bool> v(false); return v; }

        // 取路径 basename（兼容 '/' 与 '\'），与主流日志（glog/spdlog）一致。
        // strrchr 双查取靠后的分隔符，避免手写指针遍历（cppcoreguidelines 禁指针算术）
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

        // 渲染：按开关将位置前缀原地追加到 out，顺序 file:line:func，冒号分隔，方括号包裹
        // 默认配置（仅行号开）追加 "[123]"，与旧宏输出字节级一致；三项全关不追加任何字符。
        // appendTo（而非返回串）形态：emit 路径直接在正文串上拼接，消除中间临时串
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

    // 逐参追加：在每个实参前补一个空格（与旧 ostringstream 路径 `oss << " " << arg`
    // 的输出序列逐字节一致）。常用类型零分配直拼；整型手写 itoa；浮点 snprintf %g
    // （已对 4M 随机样本验证与 ostream 输出逐字节一致）；其余任意可流输出类型回退
    // 单参局部 ostringstream（罕见类型不计性能，能力不损失）。
    // bool 刻意输出 true/false（用户定版），不沿用 ostream 默认 noboolalpha 的 1/0
    static void appendLogArg(std::string& out, const char* v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, const std::string& v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, char v) { out += ' '; out += v; } // ostream 输出字符本体
    static void appendLogArg(std::string& out, bool v) { out += ' '; out += (v ? "true" : "false"); }
    template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // 手写 itoa：负号 + 十进制数字（无填充），与 ostream 默认输出一致
        // 负数 magnitude 经 make_unsigned 取模法取得：对最小值（如 INT_MIN）取负是 UB，
        // 转【无符号域】做 0 - raw 即 magnitude；符号判定仅对有符号类型做
        // （is_signed 编译期短路，无符号 T 不做比较，避免回读误判高位值）
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
        // %g = general,6 位有效数字，与 ostream 默认输出一致（先前 4M 随机样本逐字节验证）；
        // long double 需 %Lg。栈缓冲 64 字节足够（最长形如 -1.23457e-308）
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
    // （enable_if 条件与非模板重载集互斥，避免与精确匹配重载二义）
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
    // 无 tag 日志宏的默认 tag 常量（仅宏内部使用；10 字符在 SSO 内本无堆分配，
    // 静态复用仅为与 Url 常量池同惯用法，避免宏展开处重复字面量）
    static const std::string& mainLoggerTag() { static const std::string t("MainLogger"); return t; }

public:
    // 设置控制台日志代理回调（传 nullptr 恢复默认输出，取消代理回调）
    static yomk::ResStatus setConsoleLogProxy(YomkConsoleLogProxyFunc func)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerSetConsoleProxy(), yomkmsg::ConsoleLogProxy::shared(yomk::ConsoleLogProxy{func}));
    }
    // 设置文件日志代理回调（传 nullptr 恢复默认落盘，取消代理回调；与控制台代理相互独立）
    static yomk::ResStatus setFileLogProxy(YomkFileLogProxyFunc func)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerSetFileLogProxy(), yomkmsg::FileLogProxy::shared(yomk::FileLogProxy{func}));
    }
    // 控制台日志输出
    template <typename... Args>
    static yomk::ResStatus emitConsoleLogInfo(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerConsoleLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitConsoleLogWarn(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerConsoleLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitConsoleLogError(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerConsoleLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, std::move(content), tag, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitConsoleLogDebug(const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerConsoleLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, std::move(content), tag, tag}));
    }
    // 创建文件日志器
    static yomk::ResStatus createFileLogger(const std::string& logDir, const std::string& logFile)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerCreateFileLogger(), yomkmsg::LogFile::shared(yomk::LogFile{logFile, logDir}));
    }
    // 将指定日志器的缓冲落盘
    static yomk::ResStatus flushFileLog(const std::string& logFile)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerWriteFileLog(), yomkmsg::String::shared(logFile));
    }
    template <typename... Args>
    static yomk::ResStatus emitFileLogInfo(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerFileLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitFileLogWarn(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerFileLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitFileLogError(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerFileLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, std::move(content), logFile, tag}));
    }
    template <typename... Args>
    static yomk::ResStatus emitFileLogDebug(
        const std::string& logFile, const std::string& tag, const SourceLoc& loc, Args&&... args)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        std::string content;
        content.reserve(logContentReserve());
        loc.appendTo(content);
        (appendLogArg(content, std::forward<Args>(args)), ...);
        return requestStatus(Url::loggerFileLog(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, std::move(content), logFile, tag}));
    }
    // 开关各级别控制台日志（loggerName 空 = 全局主闸，与既有无参语义一致；具名 = 逐器门控）
    static yomk::ResStatus enableConsoleLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, "", loggerName}));
    }
    static yomk::ResStatus enableConsoleLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, "", loggerName}));
    }
    static yomk::ResStatus enableConsoleLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, "", loggerName}));
    }
    static yomk::ResStatus enableConsoleLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, "", loggerName}));
    }
    static yomk::ResStatus disableConsoleLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, "", loggerName}));
    }
    static yomk::ResStatus disableConsoleLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, "", loggerName}));
    }
    static yomk::ResStatus disableConsoleLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, "", loggerName}));
    }
    static yomk::ResStatus disableConsoleLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffConsoleLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, "", loggerName}));
    }
    // 开关各级别文件日志（loggerName 空 = 文件全局主闸；具名 = 逐器门控，未知名返回 ResStatus::Failed）
    static yomk::ResStatus enableFileLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, "", loggerName}));
    }
    static yomk::ResStatus enableFileLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, "", loggerName}));
    }
    static yomk::ResStatus enableFileLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, "", loggerName}));
    }
    static yomk::ResStatus enableFileLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOnFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, "", loggerName}));
    }
    static yomk::ResStatus disableFileLogDebug(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Debug, "", loggerName}));
    }
    static yomk::ResStatus disableFileLogInfo(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Info, "", loggerName}));
    }
    static yomk::ResStatus disableFileLogWarn(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Warn, "", loggerName}));
    }
    static yomk::ResStatus disableFileLogError(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerOffFileLogByLevel(), yomkmsg::Log::shared(yomk::Log{yomk::Log::LogLevel::Error, "", loggerName}));
    }
    // 按类型获取日志器名列表（type: "console"/"file"，仅返回该类型的纯名）
    static yomk::Response listLoggers(const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loggerLoggers(), yomkmsg::String::shared(type));
    }
    // 查询指定日志器状态（type: "console"/"file"；两表允许同名，须按类型精确单查）
    static yomk::Response getLoggerInfo(const std::string& loggerName, const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loggerLogger(), yomkmsg::LoggerQuery::shared(yomk::LoggerQuery{loggerName, type}));
    }
    // 查询全部日志器及逐器门控状态（返回 LoggerInfoList 清单，每条内嵌 LoggerState 状态段：
    // console 条目为控制台逐器门控层真值，file 条目为文件逐器门控层真值）
    static yomk::Response getAllLoggerInfo()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loggerAll(), nullptr);
    }
    // 查询全局主闸状态（4 级别 + proxy；type: "console"/"file"，两套主闸相互独立供两层归因；
    // 缺省 "console" 与历史无参语义一致，未知 type 返回 ResStatus::Failed）
    static yomk::Response getGlobalLoggerLevel(const std::string& type = "console")
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        if (type == "file")
        {
            return request(Url::loggerFileGlobalState(), nullptr);
        }
        if (type == "console")
        {
            return request(Url::loggerConsoleGlobalState(), nullptr);
        }
        return yomk::Response(yomk::ResStatus::Failed, "unknown logger type.");
    }
    // 删除指定日志器
    static yomk::ResStatus deleteFileLogger(const std::string& loggerName)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loggerDeleteLogger(), yomkmsg::String::shared(loggerName));
    }
    // CONTEXT_API
public:
    // 创建上下文键值（groupName 缺省 "main"；组名约定永远是 API 最后一个参数，下同）
    static yomk::ResStatus createContext(
        const std::string& ctxName, YomkPkgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxCreate(), yomkmsg::Context::shared(yomk::Context{ctxName, ctx, groupName}));
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
        yomk::Response response =
            request(Url::ctxGet(), yomkmsg::Context::shared(yomk::Context{ctxName, ctxDefault, groupName}));
        if (response.m_status == yomk::ResStatus::Succeeded)
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
            YOMK_ERR_POS_LOG("get context failed");
            return ctxDefault;
        }
    }
    // 设置上下文值（整体替换）
    static yomk::ResStatus setContext(
        const std::string& ctxName, YomkPkgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxSet(), yomkmsg::Context::shared(yomk::Context{ctxName, ctx, groupName}));
    }
    // 开关上下文校验门控
    static yomk::ResStatus enableContextChecker(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxTurnOnChecker(), yomkmsg::String::shared(groupName));
    }
    static yomk::ResStatus disableContextChecker(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxTurnOffChecker(), yomkmsg::String::shared(groupName));
    }
    // 设置上下文校验门控回调
    static yomk::ResStatus setContextChecker(
        const std::string& ctxName, YomkContextCheckerFunc checker, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::ctxSetChecker(), yomkmsg::ContextChecker::shared(yomk::ContextChecker{ctxName, checker, groupName}));
    }
    // 开关上下文变更监听器
    static yomk::ResStatus enableContextMonitor(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxTurnOnMonitor(), yomkmsg::String::shared(groupName));
    }
    static yomk::ResStatus disableContextMonitor(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxTurnOffMonitor(), yomkmsg::String::shared(groupName));
    }
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    static yomk::ResStatus setContextMonitor(const std::string& ctxName, YomkContextMonitorFunc monitor,
        bool async = false, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::ctxSetMonitor(),
            yomkmsg::ContextMonitor::shared(yomk::ContextMonitor{ctxName, monitor, async, groupName}));
    }
    // 销毁指定上下文键
    static yomk::ResStatus destroyContext(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::ctxDestroy(), yomkmsg::ContextGroupKey::shared(yomk::ContextGroupKey{groupName, ctxName}));
    }
    // 获取全部上下文键名
    static yomk::Response listContextKeys(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::ctxKeys(), yomkmsg::String::shared(groupName));
    }
    // 查询指定上下文键状态
    static yomk::Response getContextInfo(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::ctxKey(), yomkmsg::ContextGroupKey::shared(yomk::ContextGroupKey{groupName, ctxName}));
    }
    // 查询全部组名清单（返回 StringArray，对齐 listEventLoops 形态）
    static yomk::Response listContextGroups()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::ctxGroups(), nullptr);
    }
    // 查询全部组及其全部键的内省元信息（跨组聚合：返回 ContextGroupInfoList，层级为组 → 键元信息）
    static yomk::Response getAllContextInfo()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::ctxAll(), nullptr);
    }
    // EVENTLOOP_API
public:
    // 启动指定事件循环
    static yomk::ResStatus startEventLoop(
        const std::string& eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string& msgName = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::loopStart(),
            yomkmsg::EventLoop::shared(yomk::EventLoop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环线程（保留未执行事件）
    static yomk::ResStatus stopEventLoop(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loopStop(), yomkmsg::String::shared(eventLoopName));
    }
    // 异步投递事件
    static yomk::ResStatus postEvent(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::loopPost(), yomkmsg::Event::shared(yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（阻塞等待执行完成）
    static yomk::ResStatus postEventAndWait(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::loopPostWait(), yomkmsg::Event::shared(yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环并清空队列
    static yomk::ResStatus destroyEventLoop(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::loopDestroy(), yomkmsg::String::shared(eventLoopName));
    }
    // 获取全部事件循环名称列表
    static yomk::Response listEventLoops()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loopLoops(), nullptr);
    }
    // 查询指定事件循环状态与事件积压数
    static yomk::Response getEventLoopInfo(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loopLoop(), yomkmsg::String::shared(eventLoopName));
    }
    static yomk::Response getEventLoopInfo(const std::string& eventLoopName, size_t tagCount)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loopLoop(), yomkmsg::String::shared(eventLoopName + " " + std::to_string(tagCount)));
    }
    // 查询全部事件循环状态
    static yomk::Response getAllEventLoopInfo()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::loopAll(), nullptr);
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池（msgName 声明期望消息类型；description 函数说明，内容使用者自定义，均可为空）
    static yomk::ResStatus registerFunction(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
        const std::string& description = "")
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(
            Url::fpRegister(), yomkmsg::Function::shared(yomk::Function{funcName, func, msgName, description}));
    }
    // 从函数池注销函数
    static yomk::ResStatus unregisterFunction(const std::string& funcName)
    {
        if (!requireServer())
        {
            return yomk::ResStatus::Invalid;
        }
        return requestStatus(Url::fpUnregister(), yomkmsg::String::shared(funcName));
    }
    // 调用函数池中注册的函数
    static yomk::Response callFunction(const std::string& funcName, YomkPkgPtr callData)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::fpCall(), yomkmsg::CallFunction::shared(yomk::CallFunction{funcName, callData}));
    }
    // 获取函数池中全部函数名
    static yomk::Response listFunctionNames()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::fpNames(), nullptr);
    }
    // 查询单个已注册函数元信息
    static yomk::Response getFunctionInfo(const std::string& funcName)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::fpName(), yomkmsg::String::shared(funcName));
    }
    // 查询全部已注册函数元信息
    static yomk::Response getAllFunctionInfo()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::fpAll(), nullptr);
    }
    // SERVER_INFO_API
public:
    // 查询所有服务名称
    static yomk::Response listServices()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::srvServices(), nullptr);
    }
    // 查询指定服务的全部功能函数（返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，
    // 与 /function 单查、/all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    static yomk::Response listServiceFunctions(const std::string& srvName)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::srvFunctions(), yomkmsg::String::shared(srvName));
    }
    // 查询指定功能函数元信息
    static yomk::Response getServiceFunctionInfoByUrl(const std::string& url)
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::srvFunction(), yomkmsg::String::shared(url));
    }
    // 查询框架全量服务及功能函数拓扑
    static yomk::Response getAllServerInfo()
    {
        if (!requireServer())
        {
            return yomk::Response(yomk::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(Url::srvAll(), nullptr);
    }

private:
    // 全局版本号：server 注入（init）与置空（shutdown）统一经 setServer，写后
    // fetch_add(1, release) 失效所有线程的 thread_local 缓存（热路径 acquire load 读）
    static std::atomic<uint64_t>& serverEpoch()
    {
        static std::atomic<uint64_t> e(0);
        return e;
    }
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        // 每线程缓存快照：热路径 = 1 次 epoch acquire load + 缓存比对（实测 10.6 ns，
        // 对比 mutex+拷贝 22.3/84.6 ns，见性能优化文档 §五）。慢路径仅 init 后首调
        // 与失效后进入。陈旧窗口语义：并发 shutdown 时其他线程至多一调用命中缓存
        // 保活的 server，返回服务级失败 "service not found"（服务表已清空），
        // 无悬垂、无功能竞态。!cached 使空快照永不作为有效命中——未初始化期每次
        // 调用仍走慢路径上锁查询，未初始化契约消息语义不变
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
        YOMK_ERR_POS_LOG("YomkServer is not init");
        return false;
    }
    // 同步请求并收敛为三态：失败原因经错误日志承载，返回值仅保留 ResStatus
    static yomk::ResStatus requestStatus(const std::string& url, YomkPkgPtr pkg)
    {
        yomk::Response resp = request(url, std::move(pkg));
        if (resp.m_status != yomk::ResStatus::Succeeded)
        {
            YOMK_ERR_POS_LOG((resp.m_msg.empty() ? "request failed" : resp.m_msg));
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
