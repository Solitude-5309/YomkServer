#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "YomkService.h"
#include "YomkPkg.h"
#include <iostream>
#include <sstream>
#include <mutex>
#include <cstdlib>
#include <new>
#include <type_traits>

class YOMKSERVER_EXPORT YomkBoot
{
public:
    // 虚析构：boot() 内以 unique_ptr<YomkBoot> 接管指针，析构须经基类指针正确调用派生类析构
    virtual ~YomkBoot() {}
    virtual int before() = 0;
    virtual int start() = 0;
    virtual int after() = 0;
};

// YomkAPI 内部守卫宏：单例未初始化时记日志并以 ret 提前返回，不属于公共宏接口。
#define YOMK_API_REQUIRE_SERVER(ret)                \
    if (!serverSnapshot())                          \
    {                                               \
        YOMK_ERR_POS_LOG("YomkServer is not init"); \
        return ret;                                 \
    }
#define YOMK_API_GET_SERVER_OR(ret, server)         \
    auto server = serverSnapshot();                 \
    if (!server)                                    \
    {                                               \
        YOMK_ERR_POS_LOG("YomkServer is not init"); \
        return ret;                                 \
    }

class YOMKSERVER_EXPORT YomkAPI
{
    // VERSION_API
public:
    // 获取框架版本号
    static std::string version();
    // BOOT_API
public:
    // asyncThreadCount：异步线程池线程数，0 取默认（硬件并发数一半向上取整，兜底 2）；
    // 可经 YOMK_INIT(n) 配置，仅首次初始化生效
    static std::shared_ptr<YomkServer> init(std::size_t asyncThreadCount = 0)
    {
        static std::once_flag initFlag;
        std::call_once(initFlag, [asyncThreadCount]()
                       {
            auto server = YomkServer::create(asyncThreadCount);
            server->startService({"/YomkFunctionPool",
                                  "/YomkContext",
                                  "/YomkEventLoop",
                                  "/YomkLogger",
                                  "/YomkServerInfo"});
            setServer(server);
            std::atexit([]()
                        { holder().destroy(); }); });
        return serverSnapshot();
    }
    static std::shared_ptr<YomkServer> serverInstance()
    {
        return serverSnapshot();
    }
    // 关闭服务器：逐个服务执行 deinit() 后释放单例；幂等，建议在主线程调用。
    // 注意：关闭后不支持二次初始化（单进程单次初始化）
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
    template <typename T>
    static int newService(const std::string &srvName = "")
    {
        YOMK_API_GET_SERVER_OR(-1, server);
        return server->newService<T>(srvName);
    }
    // 所有权移交：注册成功后框架以 shared_ptr 持有该服务；禁止同一指针重复传入（双重释放）
    static int addService(YomkService *srv = nullptr, const std::string &srvName = "")
    {
        YOMK_API_GET_SERVER_OR(-1, server);

        if (!srv)
        {
            YOMK_ERR_POS_LOG("YomkService is null");
            return -1;
        }

        if (srvName != "")
            srv->name(srvName);

        return server->addService(srv);
    }
    static int delService(const std::string &srvName = "")
    {
        YOMK_API_GET_SERVER_OR(-1, server);

        if (srvName.empty())
        {
            YOMK_ERR_POS_LOG("service name is empty");
            return -1;
        }

        return server->delService(srvName);
    }
    static int boot(YomkBoot *boot = nullptr)
    {
        init();

        std::unique_ptr<YomkBoot> bootGuard(boot);
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
    static void asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
    {
        YOMK_API_GET_SERVER_OR((void)0, server);
        return server->asyncRequest(url, pkg, func);
    }
    static YomkResponse request(const std::string &url, YomkPkgPtr pkg)
    {
        YOMK_API_GET_SERVER_OR(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"), server);
        return server->request(url, pkg);
    }
    // LOG_API
public:
    static YomkResponse SET_CONSOLE_LOG_PROXY(YomkConsoleLogProxyFunc func)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/set_console_log_proxy", YomkMkPtr(ConsoleLogProxy, yomk::ConsoleLogProxy{func}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_INFO_TAG(const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_WARN_TAG(const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_ERROR_TAG(const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eError, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_DEBUG_TAG(const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug, oss.str(), tag}));
    }
    static YomkResponse FILE_LOG_CREATE(const std::string &logDir, const std::string &logFile)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/create_file_logger", YomkMkPtr(LogFile, yomk::LogFile{logFile, logDir}));
    }
    static YomkResponse FILE_LOG_WRITE(const std::string &logFile)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/write_file_log", YomkMkPtr(String, logFile));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_INFO_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_WARN_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_ERROR_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eError, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_DEBUG_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug, "[" + tag + "] " + oss.str(), logFile}));
    }
    static YomkResponse ON_CONSOLE_LOG_DEBUG()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug}));
    }
    static YomkResponse ON_CONSOLE_LOG_INFO()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo}));
    }
    static YomkResponse ON_CONSOLE_LOG_WARN()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn}));
    }
    static YomkResponse ON_CONSOLE_LOG_ERROR()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eError}));
    }
    static YomkResponse OFF_CONSOLE_LOG_DEBUG()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug}));
    }
    static YomkResponse OFF_CONSOLE_LOG_INFO()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo}));
    }
    static YomkResponse OFF_CONSOLE_LOG_WARN()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn}));
    }
    static YomkResponse OFF_CONSOLE_LOG_ERROR()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, yomk::Log{yomk::Log::eError}));
    }
    static YomkResponse LOGGER_INFO_LOGGERS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/loggers", nullptr);
    }
    static YomkResponse LOGGER_INFO_LOGGER(const std::string &loggerName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/logger", YomkMkPtr(String, loggerName));
    }
    static YomkResponse LOGGER_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/all", nullptr);
    }
    // CONTEXT_API
public:
    static YomkResponse CONTEXT_CREATE(const std::string &ctxName, YomkPkgPtr ctx)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/create", YomkMkPtr(Context, yomk::Context{ctxName, ctx}));
    }
    // 获取上下文值，不存在或类型不匹配时返回传入的默认值。
    // 返回值对象一经 set 发布即只读：改动请构造新对象再 CONTEXT_SET，勿原地修改（否则与并发读竞争、可能崩溃）。
    template <typename T>
    static std::shared_ptr<T> CONTEXT_GET(const std::string &msgName, const std::string &ctxName, std::shared_ptr<T> ctxDefault)
    {
        YOMK_API_REQUIRE_SERVER(ctxDefault);
        YomkResponse response = request("/YomkContext/get", YomkMkPtr(Context, yomk::Context{ctxName, ctxDefault}));
        if (response.m_status == YomkResponse::eOk)
        {
            YomkUnPackPkgT(response.m_data, msgName, T, ctxData);
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
    static YomkResponse CONTEXT_SET(const std::string &ctxName, YomkPkgPtr ctx)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/set", YomkMkPtr(Context, yomk::Context{ctxName, ctx}));
    }
    static YomkResponse CONTEXT_ON_CHECKER()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/turn_on_checker", nullptr);
    }
    static YomkResponse CONTEXT_OFF_CHECKER()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/turn_off_checker", nullptr);
    }
    // 校验回调：set 写值前调用，返回 eAccept 放行 / eReject 拒绝（拒绝则该次 set 失败、值不变）；用于门控写入，事后通知请用 CONTEXT_SET_MONITOR。
    // 回调内禁止再调用任何 Context API（checker 在写锁内执行，重入会死锁）。
    static YomkResponse CONTEXT_SET_CHECKER(const std::string &ctxName, YomkContextCheckFunc checker)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/set_checker", YomkMkPtr(ContextChecker, yomk::ContextChecker{ctxName, checker}));
    }
    static YomkResponse CONTEXT_ON_MONITOR()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/turn_on_monitor", nullptr);
    }
    static YomkResponse CONTEXT_OFF_MONITOR()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/turn_off_monitor", nullptr);
    }
    // 监控回调：仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    // async=false（默认）同步：set 返回前内联调用；async=true 异步：写锁内入单线程监控池、按提交序延迟调用，适合耗时回调。
    // 快照仅回调期有效（需留存请拷贝 ctx）；值对象只读——改动请新建对象再 set、勿原地改 ctx.m_value；需最新值请在回调内 CONTEXT_GET 重读；校验/拒绝请用 CONTEXT_SET_CHECKER。
    static YomkResponse CONTEXT_SET_MONITOR(const std::string &ctxName, YomkContextMonitorFunc monitor, bool async = false)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/set_monitor", YomkMkPtr(ContextMonitor, yomk::ContextMonitor{ctxName, monitor, async}));
    }
    static YomkResponse CONTEXT_DESTROY(const std::string &ctxName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/destroy", YomkMkPtr(String, ctxName));
    }
    static YomkResponse CONTEXT_INFO_KEYS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/keys", nullptr);
    }
    static YomkResponse CONTEXT_INFO_KEY(const std::string &ctxName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/key", YomkMkPtr(String, ctxName));
    }
    static YomkResponse CONTEXT_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/all", nullptr);
    }
    // EVENTLOOP_API
public:
    // 启动事件循环：运行中再调用为幂等（不新建线程）；对已停止未销毁的循环即重启——新开工作线程按 FIFO 续跑上次停止保留的积压事件，事件不丢失。
    static YomkResponse EVENTLOOP_START(
        const std::string &eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string &msgName = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/start", YomkMkPtr(Eventloop, yomk::Eventloop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环：仅退出工作线程、不清空队列——未执行事件保留待下次 START 续跑；停止期间投递被拒。
    static YomkResponse EVENTLOOP_STOP(const std::string &eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/stop", YomkMkPtr(String, eventLoopName));
    }
    // 异步投递事件：循环未运行（停止态）时投递被拒，返回 eNo。
    static YomkResponse EVENTLOOP_POST(const std::string &eventLoopName, YomkPkgPtr eventData, YomkServiceFunc eventHandle = nullptr, const std::string &tag = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/post", YomkMkPtr(Event, yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（等待执行完成返回）：循环未运行（停止态）时投递被拒，返回 eNo；销毁丢弃排队事件时等待者同样被释放并返回 eNo。
    static YomkResponse EVENTLOOP_POST_WAIT(const std::string &eventLoopName, YomkPkgPtr eventData, YomkServiceFunc eventHandle = nullptr, const std::string &tag = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/post_wait", YomkMkPtr(Event, yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环：先停止退出工作线程、再清空未执行的排队事件（不可续跑），并移除循环条目。
    static YomkResponse EVENTLOOP_DESTROY(const std::string &eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/destroy", YomkMkPtr(String, eventLoopName));
    }
    static YomkResponse EVENTLOOP_INFO_LOOPS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loops", nullptr);
    }
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string &eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loop", YomkMkPtr(String, eventLoopName));
    }
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string &eventLoopName, size_t tagCount)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loop", YomkMkPtr(String, eventLoopName + " " + std::to_string(tagCount)));
    }
    static YomkResponse EVENTLOOP_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/all", nullptr);
    }
    // FUNCTIONPOOL_API
public:
    static YomkResponse FUNCTIONPOOL_REGISTER(const std::string &funcName, YomkServiceFunc func, const std::string &msgName = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/register", YomkMkPtr(Function, yomk::Function{funcName, func, msgName}));
    }
    static YomkResponse FUNCTIONPOOL_UNREGISTER(const std::string &funcName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/unregister", YomkMkPtr(String, funcName));
    }
    static YomkResponse FUNCTIONPOOL_CALL(const std::string &funcName, YomkPkgPtr callData)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/call", YomkMkPtr(CallFunction, yomk::CallFunction{funcName, callData}));
    }
    static YomkResponse FUNCTIONPOOL_INFO_NAMES()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/names", nullptr);
    }
    static YomkResponse FUNCTIONPOOL_INFO_NAME(const std::string &funcName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/name", YomkMkPtr(String, funcName));
    }
    static YomkResponse FUNCTIONPOOL_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/all", nullptr);
    }
    // SERVER_INFO_API
public:
    static YomkResponse SERVER_INFO_SERVICES()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/services", nullptr);
    }
    static YomkResponse SERVER_INFO_FUNCTIONS(const std::string &srvName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/functions", YomkMkPtr(String, srvName));
    }
    static YomkResponse SERVER_INFO_FUNCTION(const std::string &url)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/function", YomkMkPtr(String, url));
    }
    static YomkResponse SERVER_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/all", nullptr);
    }

private:
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        std::lock_guard<std::mutex> lock(mtx());
        return holder().server;
    }
    static void setServer(std::shared_ptr<YomkServer> server)
    {
        std::lock_guard<std::mutex> lock(mtx());
        holder().server = std::move(server);
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
    static std::mutex &mtx()
    {
        static std::mutex *const m = new std::mutex;
        return *m;
    }
    static ServerHolder &holder()
    {
        static ServerHolder *const h = new ServerHolder;
        return *h;
    }
};
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)
#define YOMK_VERSION YomkAPI::version()
#define YOMK_INIT(...) YomkAPI::init(__VA_ARGS__)
#define YOMK_SHUTDOWN(...) YomkAPI::shutdown(__VA_ARGS__)
#define YOMK_SERVER_PTR YomkAPI::serverInstance()
#define YOMK_SERVER_P YomkAPI::serverInstance().get()
#define YOMK_NEW_SERVICE(ClassName, ...) YomkAPI::newService<ClassName>(__VA_ARGS__)
#define YOMK_ADD_SERVICE(...) YomkAPI::addService(__VA_ARGS__)
#define YOMK_DEL_SERVICE(...) YomkAPI::delService(__VA_ARGS__)
#define YOMK_BOOT(...) YomkAPI::boot(__VA_ARGS__)
#define YOMK_REQUEST(...) YomkAPI::request(__VA_ARGS__)
#define YOMK_ASYNC_REQUEST(...) YomkAPI::asyncRequest(__VA_ARGS__)
#define YOMK_SET_CONSOLE_LOG_PROXY(func) YomkAPI::SET_CONSOLE_LOG_PROXY(func)
#define YOMK_ON_CONSOLE_LOG_INFO() YomkAPI::ON_CONSOLE_LOG_INFO()
#define YOMK_ON_CONSOLE_LOG_WARN() YomkAPI::ON_CONSOLE_LOG_WARN()
#define YOMK_ON_CONSOLE_LOG_ERROR() YomkAPI::ON_CONSOLE_LOG_ERROR()
#define YOMK_ON_CONSOLE_LOG_DEBUG() YomkAPI::ON_CONSOLE_LOG_DEBUG()
#define YOMK_OFF_CONSOLE_LOG_INFO() YomkAPI::OFF_CONSOLE_LOG_INFO()
#define YOMK_OFF_CONSOLE_LOG_WARN() YomkAPI::OFF_CONSOLE_LOG_WARN()
#define YOMK_OFF_CONSOLE_LOG_ERROR() YomkAPI::OFF_CONSOLE_LOG_ERROR()
#define YOMK_OFF_CONSOLE_LOG_DEBUG() YomkAPI::OFF_CONSOLE_LOG_DEBUG()
#define YOMK_INFO(...) YomkAPI::CONSOLE_LOG_INFO_TAG("MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_INFO_TAG(tag, ...) YomkAPI::CONSOLE_LOG_INFO_TAG(tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_WARN(...) YomkAPI::CONSOLE_LOG_WARN_TAG("MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_WARN_TAG(tag, ...) YomkAPI::CONSOLE_LOG_WARN_TAG(tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_ERROR(...) YomkAPI::CONSOLE_LOG_ERROR_TAG("MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_ERROR_TAG(tag, ...) YomkAPI::CONSOLE_LOG_ERROR_TAG(tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_DEBUG(...) YomkAPI::CONSOLE_LOG_DEBUG_TAG("MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_DEBUG_TAG(tag, ...) YomkAPI::CONSOLE_LOG_DEBUG_TAG(tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_LOG_CREATE(...) YomkAPI::FILE_LOG_CREATE(__VA_ARGS__)
#define YOMK_FILE_LOG_WRITE(...) YomkAPI::FILE_LOG_WRITE(__VA_ARGS__)
#define YOMK_FILE_INFO(file, ...) YomkAPI::FILE_LOG_INFO_TAG(file, "MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_INFO_TAG(file, tag, ...) YomkAPI::FILE_LOG_INFO_TAG(file, tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_WARN(file, ...) YomkAPI::FILE_LOG_WARN_TAG(file, "MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_WARN_TAG(file, tag, ...) YomkAPI::FILE_LOG_WARN_TAG(file, tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_ERROR(file, ...) YomkAPI::FILE_LOG_ERROR_TAG(file, "MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_ERROR_TAG(file, tag, ...) YomkAPI::FILE_LOG_ERROR_TAG(file, tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_DEBUG(file, ...) YomkAPI::FILE_LOG_DEBUG_TAG(file, "MainLogger:" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_FILE_DEBUG_TAG(file, tag, ...) YomkAPI::FILE_LOG_DEBUG_TAG(file, tag ":" TO_STRING(__LINE__), __VA_ARGS__)
#define YOMK_LOGGER_INFO_LOGGERS() YomkAPI::LOGGER_INFO_LOGGERS()
#define YOMK_LOGGER_INFO_LOGGER(...) YomkAPI::LOGGER_INFO_LOGGER(__VA_ARGS__)
#define YOMK_LOGGER_INFO_ALL() YomkAPI::LOGGER_INFO_ALL()
#define YOMK_CONTEXT_CREATE(...) YomkAPI::CONTEXT_CREATE(__VA_ARGS__)
#define YOMK_CONTEXT_GET(MsgName, ...) YomkAPI::CONTEXT_GET<Yomk(MsgName)>(#MsgName, __VA_ARGS__)
#define YOMK_CONTEXT_SET(...) YomkAPI::CONTEXT_SET(__VA_ARGS__)
#define YOMK_CONTEXT_ON_CHECKER() YomkAPI::CONTEXT_ON_CHECKER()
#define YOMK_CONTEXT_OFF_CHECKER() YomkAPI::CONTEXT_OFF_CHECKER()
#define YOMK_CONTEXT_SET_CHECKER(...) YomkAPI::CONTEXT_SET_CHECKER(__VA_ARGS__)
#define YOMK_CONTEXT_ON_MONITOR() YomkAPI::CONTEXT_ON_MONITOR()
#define YOMK_CONTEXT_OFF_MONITOR() YomkAPI::CONTEXT_OFF_MONITOR()
#define YOMK_CONTEXT_SET_MONITOR(...) YomkAPI::CONTEXT_SET_MONITOR(__VA_ARGS__)
#define YOMK_CONTEXT_DESTROY(...) YomkAPI::CONTEXT_DESTROY(__VA_ARGS__)
#define YOMK_CONTEXT_INFO_KEYS() YomkAPI::CONTEXT_INFO_KEYS()
#define YOMK_CONTEXT_INFO_KEY(...) YomkAPI::CONTEXT_INFO_KEY(__VA_ARGS__)
#define YOMK_CONTEXT_INFO_ALL() YomkAPI::CONTEXT_INFO_ALL()
// 可选末位 MsgName 声明默认处理函数期望的消息类型（字符串化后仅作内省元数据，不参与运行时校验），一参/两参旧调用零改动
#define YOMK_EVENTLOOP_START_SELECT(_1, _2, _3, NAME, ...) NAME
#define YOMK_EVENTLOOP_START(...) YOMK_EVENTLOOP_START_SELECT(__VA_ARGS__, YOMK_EVENTLOOP_START_3, YOMK_EVENTLOOP_START_2, YOMK_EVENTLOOP_START_1)(__VA_ARGS__)
#define YOMK_EVENTLOOP_START_1(eventLoopName) YomkAPI::EVENTLOOP_START(eventLoopName)
#define YOMK_EVENTLOOP_START_2(eventLoopName, defaultServiceFunc) YomkAPI::EVENTLOOP_START(eventLoopName, defaultServiceFunc)
#define YOMK_EVENTLOOP_START_3(eventLoopName, defaultServiceFunc, MsgName) YomkAPI::EVENTLOOP_START(eventLoopName, defaultServiceFunc, #MsgName)
#define YOMK_EVENTLOOP_STOP(...) YomkAPI::EVENTLOOP_STOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST(...) YomkAPI::EVENTLOOP_POST(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST_WAIT(...) YomkAPI::EVENTLOOP_POST_WAIT(__VA_ARGS__)
#define YOMK_EVENTLOOP_DESTROY(...) YomkAPI::EVENTLOOP_DESTROY(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_LOOPS() YomkAPI::EVENTLOOP_INFO_LOOPS()
#define YOMK_EVENTLOOP_INFO_LOOP(...) YomkAPI::EVENTLOOP_INFO_LOOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_ALL() YomkAPI::EVENTLOOP_INFO_ALL()
// 可选末位 MsgName 声明该函数期望的消息类型（字符串化后仅作内省元数据，不参与运行时校验），两参旧调用零改动
#define YOMK_FUNCTIONPOOL_REGISTER_SELECT(_1, _2, _3, NAME, ...) NAME
#define YOMK_FUNCTIONPOOL_REGISTER(...) YOMK_FUNCTIONPOOL_REGISTER_SELECT(__VA_ARGS__, YOMK_FUNCTIONPOOL_REGISTER_3, YOMK_FUNCTIONPOOL_REGISTER_2)(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_REGISTER_2(funcName, func) YomkAPI::FUNCTIONPOOL_REGISTER(funcName, func)
#define YOMK_FUNCTIONPOOL_REGISTER_3(funcName, func, MsgName) YomkAPI::FUNCTIONPOOL_REGISTER(funcName, func, #MsgName)
#define YOMK_FUNCTIONPOOL_UNREGISTER(...) YomkAPI::FUNCTIONPOOL_UNREGISTER(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_CALL(...) YomkAPI::FUNCTIONPOOL_CALL(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_INFO_NAMES() YomkAPI::FUNCTIONPOOL_INFO_NAMES()
#define YOMK_FUNCTIONPOOL_INFO_NAME(...) YomkAPI::FUNCTIONPOOL_INFO_NAME(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_INFO_ALL() YomkAPI::FUNCTIONPOOL_INFO_ALL()
#define YOMK_SERVER_INFO_SERVICES() YomkAPI::SERVER_INFO_SERVICES()
#define YOMK_SERVER_INFO_FUNCTIONS(...) YomkAPI::SERVER_INFO_FUNCTIONS(__VA_ARGS__)
#define YOMK_SERVER_INFO_FUNCTION(...) YomkAPI::SERVER_INFO_FUNCTION(__VA_ARGS__)
#define YOMK_SERVER_INFO_ALL() YomkAPI::SERVER_INFO_ALL()