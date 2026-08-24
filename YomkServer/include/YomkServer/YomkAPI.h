#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "YomkService.h"
#include "YomkPkg.h"
#include <iostream>
#include <sstream>
#include <mutex>

using namespace yomk;

class YOMKSERVER_EXPORT YomkBoot
{
public:
    virtual int before() = 0;
    virtual int start() = 0;
    virtual int after() = 0;
};

class YOMKSERVER_EXPORT YomkAPI
{
    // VERSION_API
public:
    // 获取框架版本号（对应顶层 project(Yomk VERSION x.x.x) 定义的 VERSION）
    static std::string version();
    // BOOT_API
public:
    static std::shared_ptr<YomkServer> init()
    {
        static std::once_flag initFlag;
        std::call_once(initFlag, []()
                       {
            m_pServer = std::make_shared<YomkServer>();
            m_pServer->startService({"/YomkFunctionPool",
                                     "/YomkContext",
                                     "/YomkEventLoop",
                                     "/YomkLogger"}); });
        return m_pServer;
    }
    static std::shared_ptr<YomkServer> serverInstance()
    {
        return m_pServer;
    }
    template <typename T>
    static int newService(const std::string &srvName = "")
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return -1;
        }
        return m_pServer->newService<T>(srvName);
    }
    static int addService(YomkService *srv = nullptr, const std::string &srvName = "")
    {
        if (!m_pServer)
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

        m_pServer->addService(srv);
        return 0;
    }
    static int delService(const std::string &srvName = "")
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return -1;
        }

        if (srvName.empty())
        {
            YOMK_ERR_POS_LOG("service name is empty");
            return -1;
        }

        return m_pServer->delService(srvName);
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
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return;
        }
        return m_pServer->asyncRequest(url, pkg, func);
    }
    static YomkResponse request(const std::string &url, YomkPkgPtr pkg)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return m_pServer->request(url, pkg);
    }
    // LOG_API
public:
    static YomkResponse SET_CONSOLE_LOG_PROXY(YomkConsoleLogProxyFunc func)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/set_console_log_proxy", YomkMkPtr(ConsoleLogProxy, ConsoleLogProxy{func}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_INFO_TAG(const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, Log{Log::eInfo, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_WARN_TAG(const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, Log{Log::eWarn, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_ERROR_TAG(const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, Log{Log::eError, oss.str(), tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_DEBUG_TAG(const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/console_log", YomkMkPtr(Log, Log{Log::eDebug, oss.str(), tag}));
    }
    static YomkResponse FILE_LOG_CREATE(const std::string &logDir, const std::string &logFile)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/create_file_logger", YomkMkPtr(LogFile, LogFile{logFile, logDir}));
    }
    static YomkResponse FILE_LOG_WRITE(const std::string &logFile)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/write_file_log", YomkMkPtr(String, logFile));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_INFO_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, Log{Log::eInfo, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_WARN_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, Log{Log::eWarn, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_ERROR_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, Log{Log::eError, "[" + tag + "] " + oss.str(), logFile}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_DEBUG_TAG(const std::string &logFile, const std::string &tag, Args &&...args)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return request("/YomkLogger/file_log", YomkMkPtr(Log, Log{Log::eDebug, "[" + tag + "] " + oss.str(), logFile}));
    }
    static YomkResponse ON_CONSOLE_LOG_DEBUG()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, Log{Log::eDebug}));
    }
    static YomkResponse ON_CONSOLE_LOG_INFO()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, Log{Log::eInfo}));
    }
    static YomkResponse ON_CONSOLE_LOG_WARN()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, Log{Log::eWarn}));
    }
    static YomkResponse ON_CONSOLE_LOG_ERROR()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", YomkMkPtr(Log, Log{Log::eError}));
    }
    static YomkResponse OFF_CONSOLE_LOG_DEBUG()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, Log{Log::eDebug}));
    }
    static YomkResponse OFF_CONSOLE_LOG_INFO()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, Log{Log::eInfo}));
    }
    static YomkResponse OFF_CONSOLE_LOG_WARN()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, Log{Log::eWarn}));
    }
    static YomkResponse OFF_CONSOLE_LOG_ERROR()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", YomkMkPtr(Log, Log{Log::eError}));
    }
    // CONTEXT_API
public:
    static YomkResponse CONTEXT_CREATE(const std::string &ctxName, YomkPkgPtr ctx)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/create", YomkMkPtr(Context, Context{ctxName, ctx}));
    }
    template <typename T>
    static std::shared_ptr<T> CONTEXT_GET(const std::string &msgName, const std::string &ctxName, std::shared_ptr<T> ctxDefault)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return ctxDefault;
        }
        YomkResponse response = request("/YomkContext/get", YomkMkPtr(Context, Context{ctxName, ctxDefault}));
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
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set", YomkMkPtr(Context, Context{ctxName, ctx}));
    }
    static YomkResponse CONTEXT_ON_CHECKER()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_checker", nullptr);
    }
    static YomkResponse CONTEXT_OFF_CHECKER()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_checker", nullptr);
    }
    static YomkResponse CONTEXT_SET_CHECKER(const std::string &ctxName, YomkContextCheckFunc checker)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set_checker", YomkMkPtr(ContextChecker, ContextChecker{ctxName, checker}));
    }
    static YomkResponse CONTEXT_ON_MONITOR()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_monitor", nullptr);
    }
    static YomkResponse CONTEXT_OFF_MONITOR()
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_monitor", nullptr);
    }
    static YomkResponse CONTEXT_SET_MONITOR(const std::string &ctxName, YomkContextMonitorFunc monitor, bool async = false)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set_monitor", YomkMkPtr(ContextMonitor, ContextMonitor{ctxName, monitor, async}));
    }
    static YomkResponse CONTEXT_DESTROY(const std::string &ctxName)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/destroy", YomkMkPtr(String, ctxName));
    }
    // EVENTLOOP_API
public:
    static YomkResponse EVENTLOOP_START(
        const std::string &eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/start", YomkMkPtr(Eventloop, Eventloop{eventLoopName, m_defaultServiceFunc}));
    }
    static YomkResponse EVENTLOOP_STOP(const std::string &eventLoopName)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/stop", YomkMkPtr(String, eventLoopName));
    }
    static YomkResponse EVENTLOOP_POST(const std::string &eventLoopName, YomkPkgPtr eventData, YomkServiceFunc eventHandle = nullptr)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/post", YomkMkPtr(Event, Event{eventLoopName, eventData, eventHandle}));
    }
    static YomkResponse EVENTLOOP_POST_WAIT(const std::string &eventLoopName, YomkPkgPtr eventData, YomkServiceFunc eventHandle = nullptr)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/post_wait", YomkMkPtr(Event, Event{eventLoopName, eventData, eventHandle}));
    }
    static YomkResponse EVENTLOOP_DESTROY(const std::string &eventLoopName)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/destroy", YomkMkPtr(String, eventLoopName));
    }
    // FUNCTIONPOOL_API
public:
    static YomkResponse FUNCTIONPOOL_REGISTER(const std::string &funcName, YomkServiceFunc func)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/register", YomkMkPtr(Function, Function{funcName, func}));
    }
    static YomkResponse FUNCTIONPOOL_CALL(const std::string &funcName, YomkPkgPtr callData)
    {
        if (!m_pServer)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/call", YomkMkPtr(CallFunction, CallFunction{funcName, callData}));
    }

private:
    static std::shared_ptr<YomkServer> m_pServer;
};
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)
#define YOMK_VERSION YomkAPI::version()
#define YOMK_INIT(...) YomkAPI::init(__VA_ARGS__)
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
#define YOMK_EVENTLOOP_START(...) YomkAPI::EVENTLOOP_START(__VA_ARGS__)
#define YOMK_EVENTLOOP_STOP(...) YomkAPI::EVENTLOOP_STOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST(...) YomkAPI::EVENTLOOP_POST(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST_WAIT(...) YomkAPI::EVENTLOOP_POST_WAIT(__VA_ARGS__)
#define YOMK_EVENTLOOP_DESTROY(...) YomkAPI::EVENTLOOP_DESTROY(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_REGISTER(...) YomkAPI::FUNCTIONPOOL_REGISTER(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_CALL(...) YomkAPI::FUNCTIONPOOL_CALL(__VA_ARGS__)