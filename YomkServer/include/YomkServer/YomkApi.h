#pragma once
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "YomkBoot.h"
#include "YomkExport.h"
#include "YomkMsgs.h"
#include "YomkServer.h"
#include "YomkServerRef.h"
#include "YomkService.h"
#include "YomkDiag.h"
#include "YomkLogFmt.h"
#include "YomkSourceLoc.h"
#include "YomkUrl.h"

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
                YomkServerRef::setServer(server);
                std::atexit([]() { YomkServerRef::holder().destroy(); });
            });
    }
    // 获取全局单例，未初始化返回 nullptr
    static std::shared_ptr<YomkServer> serverInstance() { return YomkServerRef::serverSnapshot(); }
    // 关闭服务器并释放所有服务
    static void shutdown()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            return;
        }
        server->shutdown();
        YomkServerRef::setServer(nullptr);
    }
    // 创建并注册指定类型的服务
    template <typename T>
    static int newService(const std::string& srvName = "")
    {
        auto server = YomkServerRef::serverSnapshot();
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
        auto server = YomkServerRef::serverSnapshot();
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
        auto server = YomkServerRef::serverSnapshot();
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
            YOMK_DIAG_ERROR("YomkBoot before failed!");
            return ret;
        }

        ret = bootGuard->start();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot start failed!");
            return ret;
        }

        ret = bootGuard->after();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot after failed!");
            return ret;
        }

        return 0;
    }
    // REQ_API
public:
    // 异步请求服务功能函数
    static void asyncRequest(std::string_view url, YomkMsgPtr pkg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return;
        }
        return server->asyncRequest(url, pkg, func);
    }
    // 同步请求服务功能函数
    static yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return server->request(url, pkg);
    }
    // LOG_API
private:
    // emit 正文组装：位置前缀 + 逐参追加，console/file 两个核心共用
    template <typename... Args>
    static std::string buildLogContent(const YomkSourceLoc& loc, Args&&... args)
    {
        std::string content;
        content.reserve(YomkLogFmt::logContentReserve());
        loc.appendTo(content);
        (YomkLogFmt::appendLogArg(content, std::forward<Args>(args)), ...);
        return content;
    }
    // 控制台 emit 核心：Log 的 name 与 tag 字段均为 tag
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLog(yomk::payload::LogLevel lv,
        const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return requestStatus(yomk::url::loggerConsoleLog,
            yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), tag, tag}));
    }
    // 文件 emit 核心：Log 的 name 字段为 logFile
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLog(yomk::payload::LogLevel lv,
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return requestStatus(yomk::url::loggerFileLog,
            yomk::msgs::Log::shared(yomk::payload::Log{lv, buildLogContent(loc, std::forward<Args>(args)...), logFile, tag}));
    }
    // 逐级别开关公共核心：console/file × on/off × 六级别共用。
    // loggerName 空 = 全局主闸（与既有无参语义一致）；具名 = 逐器门控，
    // 文件路径未知名返回 ResStatus::Failed
    static yomk::payload::ResStatus setLogByLevel(
        bool console, bool on, yomk::payload::LogLevel lv, const std::string& loggerName)
    {
        std::string_view url = console
            ? (on ? yomk::url::loggerOnConsoleLogByLevel : yomk::url::loggerOffConsoleLogByLevel)
            : (on ? yomk::url::loggerOnFileLogByLevel : yomk::url::loggerOffFileLogByLevel);
        return requestStatus(url, yomk::msgs::Log::shared(yomk::payload::Log{lv, "", loggerName}));
    }

public:
    // 位置信息输出开关，转发至 YomkSourceLoc 内部开关（进程级生效，线程安全）
    static void setLocFileEnabled(bool on) { YomkSourceLoc::fileFlag().store(on); }
    static void setLocLineEnabled(bool on) { YomkSourceLoc::lineFlag().store(on); }
    static void setLocFuncEnabled(bool on) { YomkSourceLoc::funcFlag().store(on); }
    // 无 tag 日志宏的默认 tag 常量（仅宏内部使用）
    static const std::string& mainLoggerTag() { static const std::string t("MainLogger"); return t; }

public:
    // 设置控制台日志代理回调（传 nullptr 恢复默认输出，取消代理回调）
    static yomk::payload::ResStatus setConsoleLogProxy(YomkConsoleLogProxyFunc func)
    {
        return requestStatus(yomk::url::loggerSetConsoleProxy, yomk::msgs::ConsoleLogProxy::shared(yomk::payload::ConsoleLogProxy{func}));
    }
    // 设置文件日志代理回调（传 nullptr 恢复默认落盘，取消代理回调；与控制台代理相互独立）
    static yomk::payload::ResStatus setFileLogProxy(YomkFileLogProxyFunc func)
    {
        return requestStatus(yomk::url::loggerSetFileLogProxy, yomk::msgs::FileLogProxy::shared(yomk::payload::FileLogProxy{func}));
    }
    // 控制台日志输出（emitConsoleLog/emitFileLog 核心 + 单行转发，仅级别差异）
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogInfo(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Info, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogWarn(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Warn, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogError(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Error, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogDebug(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Debug, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogTrace(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Trace, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitConsoleLogFatal(const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitConsoleLog(yomk::payload::LogLevel::Fatal, tag, loc, std::forward<Args>(args)...);
    }
    // 创建文件日志器
    static yomk::payload::ResStatus createFileLogger(const std::string& logDir, const std::string& logFile)
    {
        return requestStatus(yomk::url::loggerCreateFileLogger, yomk::msgs::LogFile::shared(yomk::payload::LogFile{logFile, logDir}));
    }
    // 将指定日志器的缓冲落盘
    static yomk::payload::ResStatus flushFileLog(const std::string& logFile)
    {
        return requestStatus(yomk::url::loggerWriteFileLog, yomk::msgs::String::shared(logFile));
    }
    // 文件日志输出（emitConsoleLog/emitFileLog 核心 + 单行转发，仅级别差异）
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogInfo(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Info, logFile, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogWarn(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Warn, logFile, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogError(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Error, logFile, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogDebug(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Debug, logFile, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogTrace(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Trace, logFile, tag, loc, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static yomk::payload::ResStatus emitFileLogFatal(
        const std::string& logFile, const std::string& tag, const YomkSourceLoc& loc, Args&&... args)
    {
        return emitFileLog(yomk::payload::LogLevel::Fatal, logFile, tag, loc, std::forward<Args>(args)...);
    }
    // 开关各级别控制台日志（setLogByLevel 公共核心 + 单行转发；loggerName 空 = 全局主闸，具名 = 逐器门控）
    static yomk::payload::ResStatus enableConsoleLogInfo(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Info, loggerName);
    }
    static yomk::payload::ResStatus enableConsoleLogWarn(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Warn, loggerName);
    }
    static yomk::payload::ResStatus enableConsoleLogError(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Error, loggerName);
    }
    static yomk::payload::ResStatus enableConsoleLogDebug(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Debug, loggerName);
    }
    static yomk::payload::ResStatus enableConsoleLogTrace(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Trace, loggerName);
    }
    static yomk::payload::ResStatus enableConsoleLogFatal(const std::string& loggerName = "")
    {
        return setLogByLevel(true, true, yomk::payload::LogLevel::Fatal, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogInfo(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Info, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogWarn(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Warn, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogError(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Error, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogDebug(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Debug, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogTrace(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Trace, loggerName);
    }
    static yomk::payload::ResStatus disableConsoleLogFatal(const std::string& loggerName = "")
    {
        return setLogByLevel(true, false, yomk::payload::LogLevel::Fatal, loggerName);
    }
    // 开关各级别文件日志（setLogByLevel 公共核心 + 单行转发；loggerName 空 = 文件全局主闸，具名 = 逐器门控）
    static yomk::payload::ResStatus enableFileLogInfo(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Info, loggerName);
    }
    static yomk::payload::ResStatus enableFileLogWarn(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Warn, loggerName);
    }
    static yomk::payload::ResStatus enableFileLogError(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Error, loggerName);
    }
    static yomk::payload::ResStatus enableFileLogDebug(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Debug, loggerName);
    }
    static yomk::payload::ResStatus enableFileLogTrace(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Trace, loggerName);
    }
    static yomk::payload::ResStatus enableFileLogFatal(const std::string& loggerName = "")
    {
        return setLogByLevel(false, true, yomk::payload::LogLevel::Fatal, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogInfo(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Info, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogWarn(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Warn, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogError(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Error, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogDebug(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Debug, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogTrace(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Trace, loggerName);
    }
    static yomk::payload::ResStatus disableFileLogFatal(const std::string& loggerName = "")
    {
        return setLogByLevel(false, false, yomk::payload::LogLevel::Fatal, loggerName);
    }
    // 按类型获取日志器名列表（type: "console"/"file"，仅返回该类型的纯名）
    static yomk::payload::Response listLoggers(const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loggerLoggers, yomk::msgs::String::shared(type));
    }
    // 查询指定日志器状态（type: "console"/"file"；两表允许同名，须按类型精确单查）
    static yomk::payload::Response getLoggerInfo(const std::string& loggerName, const std::string& type)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loggerLogger, yomk::msgs::LoggerQuery::shared(yomk::payload::LoggerQuery{loggerName, type}));
    }
    // 查询全部日志器及逐器门控状态（返回 LoggerInfoList 清单，每条内嵌 LoggerState 状态段：
    // console 条目为控制台逐器门控层真值，file 条目为文件逐器门控层真值）
    static yomk::payload::Response getAllLoggerInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loggerAll, nullptr);
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
            return request(yomk::url::loggerFileGlobalState, nullptr);
        }
        if (type == "console")
        {
            return request(yomk::url::loggerConsoleGlobalState, nullptr);
        }
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type.");
    }
    // 删除指定日志器
    static yomk::payload::ResStatus deleteFileLogger(const std::string& loggerName)
    {
        return requestStatus(yomk::url::loggerDeleteLogger, yomk::msgs::String::shared(loggerName));
    }
    // CONTEXT_API
public:
    // 创建上下文键值（groupName 缺省 "main"；组名约定永远是 API 最后一个参数，下同）
    static yomk::payload::ResStatus createContext(
        const std::string& ctxName, YomkMsgPtr ctx, const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxCreate, yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctx, groupName}));
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
            request(yomk::url::ctxGet, yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctxDefault, groupName}));
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
        return requestStatus(yomk::url::ctxSet, yomk::msgs::Context::shared(yomk::payload::Context{ctxName, ctx, groupName}));
    }
    // 开关上下文校验门控
    static yomk::payload::ResStatus enableContextChecker(const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxTurnOnChecker, yomk::msgs::String::shared(groupName));
    }
    static yomk::payload::ResStatus disableContextChecker(const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxTurnOffChecker, yomk::msgs::String::shared(groupName));
    }
    // 设置上下文校验门控回调
    static yomk::payload::ResStatus setContextChecker(
        const std::string& ctxName, YomkContextCheckerFunc checker, const std::string& groupName = "main")
    {
        return requestStatus(
            yomk::url::ctxSetChecker, yomk::msgs::ContextChecker::shared(yomk::payload::ContextChecker{ctxName, checker, groupName}));
    }
    // 开关上下文变更监听器
    static yomk::payload::ResStatus enableContextMonitor(const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxTurnOnMonitor, yomk::msgs::String::shared(groupName));
    }
    static yomk::payload::ResStatus disableContextMonitor(const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxTurnOffMonitor, yomk::msgs::String::shared(groupName));
    }
    // 设置上下文变更监听回调（async=true 为异步通知），仅通知发生了一次 set 事件，并回传该次 set 的键值快照——不保证快照实时性；顺序上异步恒按 set 提交序送达、同步在并发 set 下不保证跨线程序。
    static yomk::payload::ResStatus setContextMonitor(const std::string& ctxName, YomkContextMonitorFunc monitor,
        bool async = false, const std::string& groupName = "main")
    {
        return requestStatus(yomk::url::ctxSetMonitor,
            yomk::msgs::ContextMonitor::shared(yomk::payload::ContextMonitor{ctxName, monitor, async, groupName}));
    }
    // 销毁指定上下文键
    static yomk::payload::ResStatus destroyContext(const std::string& ctxName, const std::string& groupName = "main")
    {
        return requestStatus(
            yomk::url::ctxDestroy, yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{groupName, ctxName}));
    }
    // 获取全部上下文键名
    static yomk::payload::Response listContextKeys(const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::ctxKeys, yomk::msgs::String::shared(groupName));
    }
    // 查询指定上下文键状态
    static yomk::payload::Response getContextInfo(const std::string& ctxName, const std::string& groupName = "main")
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::ctxKey, yomk::msgs::ContextGroupKey::shared(yomk::payload::ContextGroupKey{groupName, ctxName}));
    }
    // 查询全部组名清单（返回 StringArray，对齐 listEventLoops 形态）
    static yomk::payload::Response listContextGroups()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::ctxGroups, nullptr);
    }
    // 查询全部组及其全部键的内省元信息（跨组聚合：返回 ContextGroupInfoList，层级为组 → 键元信息）
    static yomk::payload::Response getAllContextInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::ctxAll, nullptr);
    }
    // EVENTLOOP_API
public:
    // 启动指定事件循环
    static yomk::payload::ResStatus startEventLoop(
        const std::string& eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string& msgName = "")
    {
        return requestStatus(
            yomk::url::loopStart,
            yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{eventLoopName, m_defaultServiceFunc, msgName}));
    }
    // 停止事件循环线程（保留未执行事件）
    static yomk::payload::ResStatus stopEventLoop(const std::string& eventLoopName)
    {
        return requestStatus(yomk::url::loopStop, yomk::msgs::String::shared(eventLoopName));
    }
    // 异步投递事件
    static yomk::payload::ResStatus postEvent(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        return requestStatus(
            yomk::url::loopPost, yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 同步投递事件（阻塞等待执行完成）
    static yomk::payload::ResStatus postEventAndWait(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        return requestStatus(
            yomk::url::loopPostWait, yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag)));
    }
    // 销毁事件循环并清空队列
    static yomk::payload::ResStatus destroyEventLoop(const std::string& eventLoopName)
    {
        return requestStatus(yomk::url::loopDestroy, yomk::msgs::String::shared(eventLoopName));
    }
    // 获取全部事件循环名称列表
    static yomk::payload::Response listEventLoops()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loopLoops, nullptr);
    }
    // 查询指定事件循环状态与事件积压数
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loopLoop, yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{eventLoopName}));
    }
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName, size_t tagCount)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loopLoop, yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{eventLoopName, tagCount}));
    }
    // 查询全部事件循环状态
    static yomk::payload::Response getAllEventLoopInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::loopAll, nullptr);
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池（msgName 声明期望消息类型；description 函数说明，内容使用者自定义，均可为空）
    static yomk::payload::ResStatus registerFunction(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
        const std::string& description = "")
    {
        return requestStatus(
            yomk::url::fpRegister, yomk::msgs::Function::shared(yomk::payload::Function{funcName, func, msgName, description}));
    }
    // 从函数池注销函数
    static yomk::payload::ResStatus unregisterFunction(const std::string& funcName)
    {
        return requestStatus(yomk::url::fpUnregister, yomk::msgs::String::shared(funcName));
    }
    // 调用函数池中注册的函数
    static yomk::payload::Response callFunction(const std::string& funcName, YomkMsgPtr callData)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::fpCall, yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{funcName, callData}));
    }
    // 获取函数池中全部函数名
    static yomk::payload::Response listFunctionNames()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::fpNames, nullptr);
    }
    // 查询单个已注册函数元信息
    static yomk::payload::Response getFunctionInfo(const std::string& funcName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::fpName, yomk::msgs::String::shared(funcName));
    }
    // 查询全部已注册函数元信息
    static yomk::payload::Response getAllFunctionInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::fpAll, nullptr);
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
        return request(yomk::url::srvServices, nullptr);
    }
    // 查询指定服务的全部功能函数（返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，
    // 与 /function 单查、/all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    static yomk::payload::Response listServiceFunctions(const std::string& srvName)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::srvFunctions, yomk::msgs::String::shared(srvName));
    }
    // 查询指定功能函数元信息
    static yomk::payload::Response getServiceFunctionInfoByUrl(const std::string& url)
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::srvFunction, yomk::msgs::String::shared(url));
    }
    // 查询框架全量服务及功能函数拓扑
    static yomk::payload::Response getAllServerInfo()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return request(yomk::url::srvAll, nullptr);
    }

private:
    // 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值
    static bool requireServer()
    {
        if (YomkServerRef::serverSnapshot())
            return true;
        YOMK_DIAG_ERROR("YomkServer is not init");
        return false;
    }
    // 同步请求并收敛为三态：失败原因经错误日志承载，返回值仅保留 ResStatus
    static yomk::payload::ResStatus requestStatus(std::string_view url, YomkMsgPtr pkg)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        yomk::payload::Response resp = request(url, std::move(pkg));
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.m_msg.empty() ? "request failed" : resp.m_msg));
        }
        return resp.m_status;
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
