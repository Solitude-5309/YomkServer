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
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return;
        }
        return server->asyncRequest(url, pkg, func);
    }
    // 同步请求服务功能函数
    static YomkResponse request(const std::string& url, YomkPkgPtr pkg)
    {
        auto server = serverSnapshot();
        if (!server)
        {
            YOMK_ERR_POS_LOG("YomkServer is not init");
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return server->request(url, pkg);
    }
    // LOG_API
public:
    // 设置控制台日志代理回调（传 nullptr 恢复默认输出，取消代理回调）
    static YomkResponse SET_CONSOLE_LOG_PROXY(YomkConsoleLogProxyFunc func)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/set_console_log_proxy", yomkmsg::ConsoleLogProxy::shared(yomk::ConsoleLogProxy{func}));
    }
    // 设置文件日志代理回调（传 nullptr 恢复默认落盘，取消代理回调；与控制台代理相互独立）
    static YomkResponse SET_FILE_LOG_PROXY(YomkFileLogProxyFunc func)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/set_file_log_proxy", yomkmsg::FileLogProxy::shared(yomk::FileLogProxy{func}));
    }
    // 控制台日志输出
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_INFO_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_WARN_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_ERROR_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, oss.str(), tag, tag}));
    }
    template <typename... Args>
    static YomkResponse CONSOLE_LOG_DEBUG_TAG(const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/console_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, oss.str(), tag, tag}));
    }
    // 创建文件日志器
    static YomkResponse FILE_LOG_CREATE(const std::string& logDir, const std::string& logFile)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/create_file_logger", yomkmsg::LogFile::shared(yomk::LogFile{logFile, logDir}));
    }
    // 将指定日志器的缓冲落盘
    static YomkResponse FILE_LOG_WRITE(const std::string& logFile)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/write_file_log", yomkmsg::String::shared(logFile));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_INFO_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_WARN_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_ERROR_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, oss.str(), logFile, tag}));
    }
    template <typename... Args>
    static YomkResponse FILE_LOG_DEBUG_TAG(
        const std::string& logFile, const std::string& tag, const std::string& fileLine, Args&&... args)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        std::ostringstream oss;
        oss << fileLine;
        ((oss << " " << std::forward<Args>(args)), ...);
        return request("/YomkLogger/file_log", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, oss.str(), logFile, tag}));
    }
    // 开关各级别控制台日志（loggerName 空 = 全局主闸，与既有无参语义一致；具名 = 逐器门控）
    static YomkResponse ON_CONSOLE_LOG_DEBUG(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, "", loggerName}));
    }
    static YomkResponse ON_CONSOLE_LOG_INFO(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "", loggerName}));
    }
    static YomkResponse ON_CONSOLE_LOG_WARN(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, "", loggerName}));
    }
    static YomkResponse ON_CONSOLE_LOG_ERROR(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, "", loggerName}));
    }
    static YomkResponse OFF_CONSOLE_LOG_DEBUG(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, "", loggerName}));
    }
    static YomkResponse OFF_CONSOLE_LOG_INFO(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "", loggerName}));
    }
    static YomkResponse OFF_CONSOLE_LOG_WARN(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, "", loggerName}));
    }
    static YomkResponse OFF_CONSOLE_LOG_ERROR(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_console_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, "", loggerName}));
    }
    // 开关各级别文件日志（loggerName 空 = 文件全局主闸；具名 = 逐器门控，未知名返回 eNo）
    static YomkResponse ON_FILE_LOG_DEBUG(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, "", loggerName}));
    }
    static YomkResponse ON_FILE_LOG_INFO(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "", loggerName}));
    }
    static YomkResponse ON_FILE_LOG_WARN(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, "", loggerName}));
    }
    static YomkResponse ON_FILE_LOG_ERROR(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/on_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, "", loggerName}));
    }
    static YomkResponse OFF_FILE_LOG_DEBUG(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eDebug, "", loggerName}));
    }
    static YomkResponse OFF_FILE_LOG_INFO(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eInfo, "", loggerName}));
    }
    static YomkResponse OFF_FILE_LOG_WARN(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eWarn, "", loggerName}));
    }
    static YomkResponse OFF_FILE_LOG_ERROR(const std::string& loggerName = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/off_file_log_by_level", yomkmsg::Log::shared(yomk::Log{yomk::Log::eError, "", loggerName}));
    }
    // 按类型获取日志器名列表（type: "console"/"file"，仅返回该类型的纯名）
    static YomkResponse LOGGER_INFO_LOGGERS(const std::string& type)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/loggers", yomkmsg::String::shared(type));
    }
    // 查询指定日志器状态（type: "console"/"file"；两表允许同名，须按类型精确单查）
    static YomkResponse LOGGER_INFO_LOGGER(const std::string& loggerName, const std::string& type)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/logger", yomkmsg::LoggerQuery::shared(yomk::LoggerQuery{loggerName, type}));
    }
    // 查询全部日志器及逐器门控状态（返回 LoggerInfoList 清单，每条内嵌 LoggerState 状态段：
    // console 条目为控制台逐器门控层真值，file 条目为文件逐器门控层真值）
    static YomkResponse LOGGER_INFO_ALL()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/all", nullptr);
    }
    // 查询全局主闸状态（4 级别 + proxy；type: "console"/"file"，两套主闸相互独立供两层归因；
    // 缺省 "console" 与历史无参语义一致，未知 type 返回 eNo）
    static YomkResponse LOGGER_INFO_GLOBAL(const std::string& type = "console")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        if (type == "file")
        {
            return request("/YomkLogger/file_global_state", nullptr);
        }
        if (type == "console")
        {
            return request("/YomkLogger/console_global_state", nullptr);
        }
        return YomkResponse(YomkResponse::eNo, "unknown logger type.");
    }
    // 删除指定日志器
    static YomkResponse FILE_LOG_DELETE(const std::string& loggerName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkLogger/delete_logger", yomkmsg::String::shared(loggerName));
    }
    // CONTEXT_API
public:
    // 创建上下文键值（groupName 缺省 "main"；组名约定永远是 API 最后一个参数，下同）
    static YomkResponse CONTEXT_CREATE(
        const std::string& ctxName, YomkPkgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/create", yomkmsg::Context::shared(yomk::Context{ctxName, ctx, groupName}));
    }
    // 获取上下文值（只读快照），改动请构造新对象再 CONTEXT_SET，勿原地修改（否则与并发读竞争、可能崩溃）
    template <typename T>
    static std::shared_ptr<T> CONTEXT_GET(const std::string& ctxName,
        std::shared_ptr<T> ctxDefault, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return ctxDefault;
        }
        YomkResponse response =
            request("/YomkContext/get", yomkmsg::Context::shared(yomk::Context{ctxName, ctxDefault, groupName}));
        if (response.m_status == YomkResponse::eOk)
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
    static YomkResponse CONTEXT_SET(
        const std::string& ctxName, YomkPkgPtr ctx, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set", yomkmsg::Context::shared(yomk::Context{ctxName, ctx, groupName}));
    }
    // 开关上下文校验门控
    static YomkResponse CONTEXT_ON_CHECKER(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_checker", yomkmsg::String::shared(groupName));
    }
    static YomkResponse CONTEXT_OFF_CHECKER(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_checker", yomkmsg::String::shared(groupName));
    }
    // 设置上下文校验门控回调
    static YomkResponse CONTEXT_SET_CHECKER(
        const std::string& ctxName, YomkContextCheckFunc checker, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkContext/set_checker", yomkmsg::ContextChecker::shared(yomk::ContextChecker{ctxName, checker, groupName}));
    }
    // 开关上下文变更监听器
    static YomkResponse CONTEXT_ON_MONITOR(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_on_monitor", yomkmsg::String::shared(groupName));
    }
    static YomkResponse CONTEXT_OFF_MONITOR(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/turn_off_monitor", yomkmsg::String::shared(groupName));
    }
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    static YomkResponse CONTEXT_SET_MONITOR(const std::string& ctxName, YomkContextMonitorFunc monitor,
        bool async = false, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/set_monitor",
            yomkmsg::ContextMonitor::shared(yomk::ContextMonitor{ctxName, monitor, async, groupName}));
    }
    // 销毁指定上下文键
    static YomkResponse CONTEXT_DESTROY(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkContext/destroy", yomkmsg::ContextGroupKey::shared(yomk::ContextGroupKey{groupName, ctxName}));
    }
    // 获取全部上下文键名
    static YomkResponse CONTEXT_INFO_KEYS(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/keys", yomkmsg::String::shared(groupName));
    }
    // 查询指定上下文键状态
    static YomkResponse CONTEXT_INFO_KEY(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/key", yomkmsg::ContextGroupKey::shared(yomk::ContextGroupKey{groupName, ctxName}));
    }
    // 查询全部组名清单（返回 StringArray，对齐 EVENTLOOP_INFO_LOOPS 形态）
    static YomkResponse CONTEXT_INFO_GROUPS()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkContext/groups", nullptr);
    }
    // 查询全部组及其全部键的内省元信息（跨组聚合：返回 ContextGroupInfoList，层级为组 → 键元信息）
    static YomkResponse CONTEXT_INFO_ALL()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
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
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkEventLoop/start",
            yomkmsg::Eventloop::shared(yomk::Eventloop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环线程（保留未执行事件）
    static YomkResponse EVENTLOOP_STOP(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/stop", yomkmsg::String::shared(eventLoopName));
    }
    // 异步投递事件
    static YomkResponse EVENTLOOP_POST(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkEventLoop/post", yomkmsg::Event::shared(yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（阻塞等待执行完成）
    static YomkResponse EVENTLOOP_POST_WAIT(
        const std::string& eventLoopName,
        YomkPkgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkEventLoop/post_wait", yomkmsg::Event::shared(yomk::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环并清空队列
    static YomkResponse EVENTLOOP_DESTROY(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/destroy", yomkmsg::String::shared(eventLoopName));
    }
    // 获取全部事件循环名称列表
    static YomkResponse EVENTLOOP_INFO_LOOPS()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/loops", nullptr);
    }
    // 查询指定事件循环状态与事件积压数
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/loop", yomkmsg::String::shared(eventLoopName));
    }
    static YomkResponse EVENTLOOP_INFO_LOOP(const std::string& eventLoopName, size_t tagCount)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/loop", yomkmsg::String::shared(eventLoopName + " " + std::to_string(tagCount)));
    }
    // 查询全部事件循环状态
    static YomkResponse EVENTLOOP_INFO_ALL()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkEventLoop/all", nullptr);
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池（msgName 声明期望消息类型；description 函数说明，内容使用者自定义，均可为空）
    static YomkResponse FUNCTIONPOOL_REGISTER(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
        const std::string& description = "")
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request(
            "/YomkFunctionPool/register", yomkmsg::Function::shared(yomk::Function{funcName, func, msgName, description}));
    }
    // 从函数池注销函数
    static YomkResponse FUNCTIONPOOL_UNREGISTER(const std::string& funcName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/unregister", yomkmsg::String::shared(funcName));
    }
    // 调用函数池中注册的函数
    static YomkResponse FUNCTIONPOOL_CALL(const std::string& funcName, YomkPkgPtr callData)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/call", yomkmsg::CallFunction::shared(yomk::CallFunction{funcName, callData}));
    }
    // 获取函数池中全部函数名
    static YomkResponse FUNCTIONPOOL_INFO_NAMES()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/names", nullptr);
    }
    // 查询单个已注册函数元信息
    static YomkResponse FUNCTIONPOOL_INFO_NAME(const std::string& funcName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/name", yomkmsg::String::shared(funcName));
    }
    // 查询全部已注册函数元信息
    static YomkResponse FUNCTIONPOOL_INFO_ALL()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkFunctionPool/all", nullptr);
    }
    // SERVER_INFO_API
public:
    // 查询所有服务名称
    static YomkResponse SERVER_INFO_SERVICES()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkServerInfo/services", nullptr);
    }
    // 查询指定服务的全部功能函数（返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，
    // 与 /function 单查、/all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    static YomkResponse SERVER_INFO_FUNCTIONS(const std::string& srvName)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkServerInfo/functions", yomkmsg::String::shared(srvName));
    }
    // 查询指定功能函数元信息
    static YomkResponse SERVER_INFO_FUNCTION(const std::string& url)
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkServerInfo/function", yomkmsg::String::shared(url));
    }
    // 查询框架全量服务及功能函数拓扑
    static YomkResponse SERVER_INFO_ALL()
    {
        if (!requireServer())
        {
            return YomkResponse(YomkResponse::eInvalid, "YomkServer is not init");
        }
        return request("/YomkServerInfo/all", nullptr);
    }

private:
    static std::shared_ptr<YomkServer> serverSnapshot()
    {
        std::lock_guard<std::mutex> lock(mtx());
        return holder().server;
    }
    // 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值
    static bool requireServer()
    {
        if (serverSnapshot())
            return true;
        YOMK_ERR_POS_LOG("YomkServer is not init");
        return false;
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
#define YOMK_STRINGIFY(x) #x
#define YOMK_TO_STRING(x) YOMK_STRINGIFY(x)
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
#define YOMK_SET_FILE_LOG_PROXY(func) YomkAPI::SET_FILE_LOG_PROXY(func)
#define YOMK_ON_CONSOLE_LOG_INFO(...) YomkAPI::ON_CONSOLE_LOG_INFO(__VA_ARGS__)
#define YOMK_ON_CONSOLE_LOG_WARN(...) YomkAPI::ON_CONSOLE_LOG_WARN(__VA_ARGS__)
#define YOMK_ON_CONSOLE_LOG_ERROR(...) YomkAPI::ON_CONSOLE_LOG_ERROR(__VA_ARGS__)
#define YOMK_ON_CONSOLE_LOG_DEBUG(...) YomkAPI::ON_CONSOLE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_OFF_CONSOLE_LOG_INFO(...) YomkAPI::OFF_CONSOLE_LOG_INFO(__VA_ARGS__)
#define YOMK_OFF_CONSOLE_LOG_WARN(...) YomkAPI::OFF_CONSOLE_LOG_WARN(__VA_ARGS__)
#define YOMK_OFF_CONSOLE_LOG_ERROR(...) YomkAPI::OFF_CONSOLE_LOG_ERROR(__VA_ARGS__)
#define YOMK_OFF_CONSOLE_LOG_DEBUG(...) YomkAPI::OFF_CONSOLE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_ON_FILE_LOG_INFO(...) YomkAPI::ON_FILE_LOG_INFO(__VA_ARGS__)
#define YOMK_ON_FILE_LOG_WARN(...) YomkAPI::ON_FILE_LOG_WARN(__VA_ARGS__)
#define YOMK_ON_FILE_LOG_ERROR(...) YomkAPI::ON_FILE_LOG_ERROR(__VA_ARGS__)
#define YOMK_ON_FILE_LOG_DEBUG(...) YomkAPI::ON_FILE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_OFF_FILE_LOG_INFO(...) YomkAPI::OFF_FILE_LOG_INFO(__VA_ARGS__)
#define YOMK_OFF_FILE_LOG_WARN(...) YomkAPI::OFF_FILE_LOG_WARN(__VA_ARGS__)
#define YOMK_OFF_FILE_LOG_ERROR(...) YomkAPI::OFF_FILE_LOG_ERROR(__VA_ARGS__)
#define YOMK_OFF_FILE_LOG_DEBUG(...) YomkAPI::OFF_FILE_LOG_DEBUG(__VA_ARGS__)
#define YOMK_INFO(...) YomkAPI::CONSOLE_LOG_INFO_TAG("MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_INFO_TAG(tag, ...) YomkAPI::CONSOLE_LOG_INFO_TAG(tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_WARN(...) YomkAPI::CONSOLE_LOG_WARN_TAG("MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_WARN_TAG(tag, ...) YomkAPI::CONSOLE_LOG_WARN_TAG(tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_ERROR(...) YomkAPI::CONSOLE_LOG_ERROR_TAG("MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_ERROR_TAG(tag, ...) YomkAPI::CONSOLE_LOG_ERROR_TAG(tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_DEBUG(...) YomkAPI::CONSOLE_LOG_DEBUG_TAG("MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_DEBUG_TAG(tag, ...) YomkAPI::CONSOLE_LOG_DEBUG_TAG(tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_LOG_CREATE(...) YomkAPI::FILE_LOG_CREATE(__VA_ARGS__)
#define YOMK_FILE_LOG_WRITE(...) YomkAPI::FILE_LOG_WRITE(__VA_ARGS__)
#define YOMK_FILE_INFO(file, ...) \
    YomkAPI::FILE_LOG_INFO_TAG(file, "MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_INFO_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_INFO_TAG(file, tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_WARN(file, ...) \
    YomkAPI::FILE_LOG_WARN_TAG(file, "MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_WARN_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_WARN_TAG(file, tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_ERROR(file, ...) \
    YomkAPI::FILE_LOG_ERROR_TAG(file, "MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_ERROR_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_ERROR_TAG(file, tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_DEBUG(file, ...) \
    YomkAPI::FILE_LOG_DEBUG_TAG(file, "MainLogger", "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_FILE_DEBUG_TAG(file, tag, ...) \
    YomkAPI::FILE_LOG_DEBUG_TAG(file, tag, "[" YOMK_TO_STRING(__LINE__) "]", __VA_ARGS__)
#define YOMK_LOGGER_INFO_LOGGERS(type) YomkAPI::LOGGER_INFO_LOGGERS(type)
#define YOMK_LOGGER_INFO_LOGGER(...) YomkAPI::LOGGER_INFO_LOGGER(__VA_ARGS__)
#define YOMK_LOGGER_INFO_ALL() YomkAPI::LOGGER_INFO_ALL()
#define YOMK_LOGGER_INFO_GLOBAL(...) YomkAPI::LOGGER_INFO_GLOBAL(__VA_ARGS__)
#define YOMK_FILE_LOG_DELETE(...) YomkAPI::FILE_LOG_DELETE(__VA_ARGS__)
#define YOMK_CONTEXT_CREATE(...) YomkAPI::CONTEXT_CREATE(__VA_ARGS__)
#define YOMK_CONTEXT_SET(...) YomkAPI::CONTEXT_SET(__VA_ARGS__)
#define YOMK_CONTEXT_ON_CHECKER(...) YomkAPI::CONTEXT_ON_CHECKER(__VA_ARGS__)
#define YOMK_CONTEXT_OFF_CHECKER(...) YomkAPI::CONTEXT_OFF_CHECKER(__VA_ARGS__)
#define YOMK_CONTEXT_SET_CHECKER(...) YomkAPI::CONTEXT_SET_CHECKER(__VA_ARGS__)
#define YOMK_CONTEXT_ON_MONITOR(...) YomkAPI::CONTEXT_ON_MONITOR(__VA_ARGS__)
#define YOMK_CONTEXT_OFF_MONITOR(...) YomkAPI::CONTEXT_OFF_MONITOR(__VA_ARGS__)
#define YOMK_CONTEXT_SET_MONITOR(...) YomkAPI::CONTEXT_SET_MONITOR(__VA_ARGS__)
#define YOMK_CONTEXT_DESTROY(...) YomkAPI::CONTEXT_DESTROY(__VA_ARGS__)
#define YOMK_CONTEXT_INFO_KEYS(...) YomkAPI::CONTEXT_INFO_KEYS(__VA_ARGS__)
#define YOMK_CONTEXT_INFO_KEY(...) YomkAPI::CONTEXT_INFO_KEY(__VA_ARGS__)
#define YOMK_CONTEXT_INFO_GROUPS() YomkAPI::CONTEXT_INFO_GROUPS()
#define YOMK_CONTEXT_INFO_ALL() YomkAPI::CONTEXT_INFO_ALL()
#define YOMK_EVENTLOOP_STOP(...) YomkAPI::EVENTLOOP_STOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST(...) YomkAPI::EVENTLOOP_POST(__VA_ARGS__)
#define YOMK_EVENTLOOP_POST_WAIT(...) YomkAPI::EVENTLOOP_POST_WAIT(__VA_ARGS__)
#define YOMK_EVENTLOOP_DESTROY(...) YomkAPI::EVENTLOOP_DESTROY(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_LOOPS() YomkAPI::EVENTLOOP_INFO_LOOPS()
#define YOMK_EVENTLOOP_INFO_LOOP(...) YomkAPI::EVENTLOOP_INFO_LOOP(__VA_ARGS__)
#define YOMK_EVENTLOOP_INFO_ALL() YomkAPI::EVENTLOOP_INFO_ALL()
#define YOMK_FUNCTIONPOOL_UNREGISTER(...) YomkAPI::FUNCTIONPOOL_UNREGISTER(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_CALL(...) YomkAPI::FUNCTIONPOOL_CALL(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_INFO_NAMES() YomkAPI::FUNCTIONPOOL_INFO_NAMES()
#define YOMK_FUNCTIONPOOL_INFO_NAME(...) YomkAPI::FUNCTIONPOOL_INFO_NAME(__VA_ARGS__)
#define YOMK_FUNCTIONPOOL_INFO_ALL() YomkAPI::FUNCTIONPOOL_INFO_ALL()
#define YOMK_SERVER_INFO_SERVICES() YomkAPI::SERVER_INFO_SERVICES()
#define YOMK_SERVER_INFO_FUNCTIONS(...) YomkAPI::SERVER_INFO_FUNCTIONS(__VA_ARGS__)
#define YOMK_SERVER_INFO_FUNCTION(...) YomkAPI::SERVER_INFO_FUNCTION(__VA_ARGS__)
#define YOMK_SERVER_INFO_ALL() YomkAPI::SERVER_INFO_ALL()