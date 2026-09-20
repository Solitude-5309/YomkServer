#pragma once
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <new>
#include <sstream>
#include <type_traits>

#include "YomkDefine.h"
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
    static std::shared_ptr<YomkServer> init(std::size_t asyncThreadCount = 0)
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
        return serverSnapshot();
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
        YOMK_API_GET_SERVER_OR(-1, server);
        return server->newService<T>(srvName);
    }
    // 注册已有服务实例（所有权移交给框架）
    static int addService(YomkService* srv = nullptr, const std::string& srvName = "")
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
    // 按名称注销服务
    static int delService(const std::string& srvName = "")
    {
        YOMK_API_GET_SERVER_OR(-1, server);

        if (srvName.empty())
        {
            YOMK_ERR_POS_LOG("service name is empty");
            return -1;
        }

        return server->delService(srvName);
    }
    // 初始化并执行启动编排流程
    static int boot(YomkBoot* boot = nullptr)
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
    // 异步请求服务功能函数
    static void asyncRequest(const std::string& url, YomkPkgPtr pkg, YomkResponseFunc func)
    {
        YOMK_API_GET_SERVER_OR((void)0, server);
        return server->asyncRequest(url, pkg, func);
    }
    // 同步请求服务功能函数
    static YomkResponse request(const std::string& url, YomkPkgPtr pkg)
    {
        YOMK_API_GET_SERVER_OR(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"), server);
        return server->request(url, pkg);
    }
    // LOG_API
public:
    // 设置控制台日志代理回调（传 nullptr 恢复默认输出，取消代理回调）
    static YomkResponse SET_CONSOLE_LOG_PROXY(YomkConsoleLogProxyFunc func)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/set_console_log_proxy", YomkMkPtr(ConsoleLogProxy, yomk::ConsoleLogProxy{func}));
    }
    // 控制台日志输出
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_INFO_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_WARN_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_ERROR_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eError, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_DEBUG_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug, oss.str(), tag, tag}));
    }
    // 创建文件日志器
    static YomkResponse FILE_LOG_CREATE(const std::string& logDir, const std::string& logFile)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/create_file_logger", YomkMkPtr(LogFile, yomk::LogFile{logFile, logDir}));
    }
    // 将指定日志器的缓冲落盘
    static YomkResponse FILE_LOG_WRITE(const std::string& logFile)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/write_file_log", YomkMkPtr(String, logFile));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_INFO_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eInfo, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_WARN_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eWarn, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_ERROR_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eError, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_DEBUG_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", YomkMkPtr(Log, yomk::Log{yomk::Log::eDebug, oss.str(), logFile, tag}));
    }
    // 开关各级别控制台日志
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
    // 获取全部日志器列表
    static YomkResponse LOGGER_INFO_LOGGERS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/loggers", nullptr);
    }
    // 查询指定日志器状态
    static YomkResponse LOGGER_INFO_LOGGER(const std::string& loggerName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/logger", YomkMkPtr(String, loggerName));
    }
    // 查询全部日志器及开关配置状态
    static YomkResponse LOGGER_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/all", nullptr);
    }
    // 删除指定日志器
    static YomkResponse FILE_LOG_DELETE(const std::string& loggerName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkLogger/delete_logger", YomkMkPtr(String, loggerName));
    }
    // CONTEXT_API
public:
    // 创建上下文键值
    static YomkResponse CONTEXT_CREATE(const std::string& ctxName, YomkPkgPtr ctx)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/create", YomkMkPtr(Context, yomk::Context{ctxName, ctx}));
    }
    // 获取上下文值（只读快照），改动请构造新对象再 CONTEXT_SET，勿原地修改（否则与并发读竞争、可能崩溃）
    template <typename T>
    static std::shared_ptr<T> CONTEXT_GET(
        const std::string& msgName, const std::string& ctxName, std::shared_ptr<T> ctxDefault)
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
    // 设置上下文值（整体替换）
    static YomkResponse CONTEXT_SET(const std::string& ctxName, YomkPkgPtr ctx)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/set", YomkMkPtr(Context, yomk::Context{ctxName, ctx}));
    }
    // 开关上下文校验门控
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
    // 设置上下文校验门控回调
    static YomkResponse CONTEXT_SET_CHECKER(const std::string& ctxName, YomkContextCheckFunc checker)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/set_checker", YomkMkPtr(ContextChecker, yomk::ContextChecker{ctxName, checker}));
    }
    // 开关上下文变更监听器
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
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    static YomkResponse CONTEXT_SET_MONITOR(
        const std::string& ctxName, YomkContextMonitorFunc monitor, bool async = false)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request(
            "/YomkContext/set_monitor", YomkMkPtr(ContextMonitor, yomk::ContextMonitor{ctxName, monitor, async}));
    }
    // 销毁指定上下文键
    static YomkResponse CONTEXT_DESTROY(const std::string& ctxName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/destroy", YomkMkPtr(String, ctxName));
    }
    // 获取全部上下文键名
    static YomkResponse CONTEXT_INFO_KEYS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/keys", nullptr);
    }
    // 查询指定上下文键状态
    static YomkResponse CONTEXT_INFO_KEY(const std::string& ctxName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/key", YomkMkPtr(String, ctxName));
    }
    // 查询全部上下文键及其数据类型
    static YomkResponse CONTEXT_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkContext/all", nullptr);
    }
    // EVENTLOOP_API
public:
    // 启动指定事件循环
    static YomkResponse EVENTLOOP_START(
        const std::string& eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string& msgName = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request(
            "/YomkEventLoop/start",
            YomkMkPtr(Eventloop, yomk::Eventloop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环线程（保留未执行事件）
    static YomkResponse EVENTLOOP_STOP(const std::string& eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/stop", YomkMkPtr(String, eventLoopName));
    }
    // 异步投递事件
    static YomkResponse EVENTLOOP_POST(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request(
            "/YomkEventLoop/post", YomkMkPtr(Event, yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（阻塞等待执行完成）
    static YomkResponse EVENTLOOP_POST_WAIT(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request(
            "/YomkEventLoop/post_wait", YomkMkPtr(Event, yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环并清空队列
    static YomkResponse EVENTLOOP_DESTROY(const std::string& eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/destroy", YomkMkPtr(String, eventLoopName));
    }
    // 获取全部事件循环名称列表
    static YomkResponse EVENTLOOP_INFO_LOOPS()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loops", nullptr);
    }
    // 查询指定事件循环状态与事件积压数
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string& eventLoopName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loop", YomkMkPtr(String, eventLoopName));
    }
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string& eventLoopName, size_t tagCount)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/loop", YomkMkPtr(String, eventLoopName + " " + std::to_string(tagCount)));
    }
    // 查询全部事件循环状态
    static YomkResponse EVENTLOOP_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkEventLoop/all", nullptr);
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池
    static YomkResponse FUNCTIONPOOL_REGISTER(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "")
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/register", YomkMkPtr(Function, yomk::Function{funcName, func, msgName}));
    }
    // 从函数池注销函数
    static YomkResponse FUNCTIONPOOL_UNREGISTER(const std::string& funcName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/unregister", YomkMkPtr(String, funcName));
    }
    // 调用函数池中注册的函数
    static YomkResponse FUNCTIONPOOL_CALL(const std::string& funcName, YomkPkgPtr callData)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/call", YomkMkPtr(CallFunction, yomk::CallFunction{funcName, callData}));
    }
    // 获取函数池中全部函数名
    static YomkResponse FUNCTIONPOOL_INFO_NAMES()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/names", nullptr);
    }
    // 查询单个已注册函数元信息
    static YomkResponse FUNCTIONPOOL_INFO_NAME(const std::string& funcName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/name", YomkMkPtr(String, funcName));
    }
    // 查询全部已注册函数元信息
    static YomkResponse FUNCTIONPOOL_INFO_ALL()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkFunctionPool/all", nullptr);
    }
    // SERVER_INFO_API
public:
    // 查询所有服务名称
    static YomkResponse SERVER_INFO_SERVICES()
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/services", nullptr);
    }
    // 查询指定服务的全部功能函数
    static YomkResponse SERVER_INFO_FUNCTIONS(const std::string& srvName)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/functions", YomkMkPtr(String, srvName));
    }
    // 查询指定功能函数元信息
    static YomkResponse SERVER_INFO_FUNCTION(const std::string& url)
    {
        YOMK_API_REQUIRE_SERVER(YomkResponse(YomkResponse::eInvalid, "YomkServer is not init"));
        return request("/YomkServerInfo/function", YomkMkPtr(String, url));
    }
    // 查询框架全量服务及功能函数拓扑
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
#define YOMK_INFO(...) YomkAPI::CONSOLE_LOG_INFO_TAG("MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_INFO_TAG(tag, ...) YomkAPI::CONSOLE_LOG_INFO_TAG(tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_WARN(...) YomkAPI::CONSOLE_LOG_WARN_TAG("MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_WARN_TAG(tag, ...) YomkAPI::CONSOLE_LOG_WARN_TAG(tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_ERROR(...) YomkAPI::CONSOLE_LOG_ERROR_TAG("MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_ERROR_TAG(tag, ...) YomkAPI::CONSOLE_LOG_ERROR_TAG(tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_DEBUG(...) YomkAPI::CONSOLE_LOG_DEBUG_TAG("MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_DEBUG_TAG(tag, ...) YomkAPI::CONSOLE_LOG_DEBUG_TAG(tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_LOG_CREATE(...) YomkAPI::FILE_LOG_CREATE(__VA_ARGS__)
#define YOMK_FILE_LOG_WRITE(...) YomkAPI::FILE_LOG_WRITE(__VA_ARGS__)
#define YOMK_FILE_INFO(file, ...) \
    YomkAPI::FILE_LOG_INFO_TAG(file, "MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_INFO_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_INFO_TAG(file, tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_WARN(file, ...) \
    YomkAPI::FILE_LOG_WARN_TAG(file, "MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_WARN_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_WARN_TAG(file, tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_ERROR(file, ...) \
    YomkAPI::FILE_LOG_ERROR_TAG(file, "MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_ERROR_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_ERROR_TAG(file, tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_DEBUG(file, ...) \
    YomkAPI::FILE_LOG_DEBUG_TAG(file, "MainLogger", "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_DEBUG_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_DEBUG_TAG(file, tag, "[" TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_LOGGER_INFO_LOGGERS() YomkAPI::LOGGER_INFO_LOGGERS()
#define YOMK_LOGGER_INFO_LOGGER(...) YomkAPI::LOGGER_INFO_LOGGER(__VA_ARGS__)
#define YOMK_LOGGER_INFO_ALL() YomkAPI::LOGGER_INFO_ALL()
#define YOMK_FILE_LOG_DELETE(...) YomkAPI::FILE_LOG_DELETE(__VA_ARGS__)
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
#define YOMK_EVENTLOOP_START_SELECT(_1, _2, _3, NAME, ...) NAME
#define YOMK_EVENTLOOP_START(...)                                                                                    \
    YOMK_EVENTLOOP_START_SELECT(__VA_ARGS__, YOMK_EVENTLOOP_START_3, YOMK_EVENTLOOP_START_2, YOMK_EVENTLOOP_START_1) \
    (__VA_ARGS__)
#define YOMK_EVENTLOOP_START_1(eventLoopName) YomkAPI::EVENTLOOP_START(eventLoopName)
#define YOMK_EVENTLOOP_START_2(eventLoopName, defaultServiceFunc) \
    YomkAPI::EVENTLOOP_START(eventLoopName, defaultServiceFunc)
#define YOMK_EVENTLOOP_START_3(eventLoopName, defaultServiceFunc, MsgName) \
    YomkAPI::EVENTLOOP_START(eventLoopName, defaultServiceFunc, #MsgName)
#define YOMK_EVENTLOOP_STOP(...) YomkAPI::EVENTLOOP_STOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST(...) YomkAPI::EVENTLOOP_POST(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST_WAIT(...) YomkAPI::EVENTLOOP_POST_WAIT(__VA_ARGS__)
#define YOMK_EVENTLOOP_DESTROY(...) YomkAPI::EVENTLOOP_DESTROY(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_LOOPS() YomkAPI::EVENTLOOP_INFO_LOOPS()
#define YOMK_EVENTLOOP_INFO_LOOP(...) YomkAPI::EVENTLOOP_INFO_LOOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_ALL() YomkAPI::EVENTLOOP_INFO_ALL()
#define YOMK_FUNCTIONPOOL_REGISTER_SELECT(_1, _2, _3, NAME, ...) NAME
#define YOMK_FUNCTIONPOOL_REGISTER(...)                                                                        \
    YOMK_FUNCTIONPOOL_REGISTER_SELECT(__VA_ARGS__, YOMK_FUNCTIONPOOL_REGISTER_3, YOMK_FUNCTIONPOOL_REGISTER_2) \
    (__VA_ARGS__)
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