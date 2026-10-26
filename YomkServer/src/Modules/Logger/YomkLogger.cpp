#include "YomkLogger.h"

#include <iostream>

// 初始化列表须按 YomkLogger.h 声明序书写（-Wreorder）
YomkLogger::YomkLogger(YomkServer* server)
    : YomkService(server),
      m_showConsoleDebugLog(true),
      m_showConsoleInfoLog(true),
      m_showConsoleWarningLog(true),
      m_showConsoleErrorLog(true),
      m_showFileDebugLog(true),
      m_showFileInfoLog(true),
      m_showFileWarningLog(true),
      m_showFileErrorLog(true),
      m_consoleLogProxy(false),
      m_consoleLogProxyFunc(nullptr)
{
    name("/YomkLogger");
    m_consoleLoggers["MainLogger"] = std::make_shared<ConsoleLogger>();
}

YomkLogger::~YomkLogger() {}

int YomkLogger::init()
{
    YomkInstallFunc("/set_console_log_proxy", YomkLogger::setConsoleLogProxy, ConsoleLogProxy);
    YomkInstallFunc("/console_log", YomkLogger::consoleLog, Log);
    YomkInstallFunc("/create_file_logger", YomkLogger::createFileLogger, LogFile);
    YomkInstallFunc("/file_log", YomkLogger::fileLog, Log);
    YomkInstallFunc("/write_file_log", YomkLogger::writeFileLog, String);
    YomkInstallFunc("/delete_logger", YomkLogger::deleteLogger, String);
    YomkInstallFunc("/off_console_log_by_level", YomkLogger::offConsoleLogByLevel, Log);
    YomkInstallFunc("/on_console_log_by_level", YomkLogger::onConsoleLogByLevel, Log);
    YomkInstallFunc("/loggers", YomkLogger::loggers, String);
    YomkInstallFunc("/logger", YomkLogger::loggerInfo, LoggerQuery);
    YomkInstallFunc("/all", YomkLogger::listAll);
    YomkInstallFunc("/console_global_state", YomkLogger::consoleGlobalState);
    YomkInstallFunc("/off_file_log_by_level", YomkLogger::offFileLogByLevel, Log);
    YomkInstallFunc("/on_file_log_by_level", YomkLogger::onFileLogByLevel, Log);
    YomkInstallFunc("/file_global_state", YomkLogger::fileGlobalState);
    return 0;
}

YomkResponse YomkLogger::consoleLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);
    if (log->d.m_logger.empty())
    {
        YOMK_ERR_POS_LOG("console logger name is empty, use MainLogger");
        log->d.m_logger = "MainLogger";
    }

    // 锁内拷贝 proxy 快照，锁外调用回调（避免用户回调持锁重入）
    bool proxyEnabled = false;
    YomkConsoleLogProxyFunc proxyFunc;
    {
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        proxyEnabled = m_consoleLogProxy;
        proxyFunc = m_consoleLogProxyFunc;
    }
    if (proxyEnabled && proxyFunc && !proxyFunc(log->d))
    {
        return YomkResponse(YomkResponse::eOk, "console log proxy is success.");
    }

    // 双检锁：读锁 miss 后升级写锁二次查找，logger 指针拷出锁外使用
    ConsoleLoggerPtr consoleLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(log->d.m_logger);
        if (itLogger != m_consoleLoggers.end())
        {
            consoleLogger = itLogger->second;
        }
    }
    if (!consoleLogger)
    {
        std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(log->d.m_logger);
        if (itLogger == m_consoleLoggers.end())
        {
            std::shared_ptr<ConsoleLogger> newLogger = std::make_shared<ConsoleLogger>();
            newLogger->setName(log->d.m_logger);
            itLogger = m_consoleLoggers.emplace(log->d.m_logger, newLogger).first;
        }
        consoleLogger = itLogger->second;
    }

    switch (log->d.m_level)
    {
        case Log::eInfo:
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::eInfo, log->d.m_tag, log->d.m_log);
            break;
        case Log::eWarn:
            if (m_showConsoleWarningLog.load())
                consoleLogger->log(ConsoleLogger::eWarn, log->d.m_tag, log->d.m_log);
            break;
        case Log::eError:
            if (m_showConsoleErrorLog.load())
                consoleLogger->log(ConsoleLogger::eError, log->d.m_tag, log->d.m_log);
            break;
        case Log::eDebug:
            if (m_showConsoleDebugLog.load())
                consoleLogger->log(ConsoleLogger::eDebug, log->d.m_tag, log->d.m_log);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, use Info");
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::eInfo, log->d.m_tag, log->d.m_log);
            break;
    }

    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::setConsoleLogProxy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ConsoleLogProxy, consoleLogProxy);
    // 与 consoleLog 读侧互斥
    std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    // 传空回调即卸载 proxy、恢复默认输出；两字段同锁一致更新
    m_consoleLogProxy = (m_consoleLogProxyFunc != nullptr);
    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, LogFile, logFile);

    // 空名/空目录拒绝：eInvalid（参数无效惯例）
    if (logFile->d.m_logger.empty())
    {
        YOMK_ERR_POS_LOG("logger name is empty.");
        return YomkResponse(YomkResponse::eInvalid, "logger name is empty.");
    }
    if (logFile->d.m_dir.empty())
    {
        YOMK_ERR_POS_LOG("logger dir is empty.");
        return YomkResponse(YomkResponse::eInvalid, "logger dir is empty.");
    }

    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if (m_fileLoggers.find(logFile->d.m_logger) != m_fileLoggers.end())
    {
        return YomkResponse(YomkResponse::eNo, "logger name already exists.");
    }

    std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
    fileLogger->setName(logFile->d.m_logger);
    fileLogger->setDir(logFile->d.m_dir);
    if (!fileLogger->init())
    {
        YOMK_ERR_POS_LOG("init file logger failed: " + logFile->d.m_logger);
        return YomkResponse(YomkResponse::eNo, "init file logger failed.");
    }
    m_fileLoggers.emplace(logFile->d.m_logger, fileLogger);

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::fileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

    if (log->d.m_logger.empty())
    {
        YOMK_ERR_POS_LOG("file logger name is empty.");
        return YomkResponse(YomkResponse::eNo, "file logger name is empty.");
    }

    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);

    auto itLogger = m_fileLoggers.find(log->d.m_logger);
    if (itLogger == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("file logger: " + log->d.m_logger + " not found.");
        return YomkResponse(YomkResponse::eNo, "file logger not found.");
    }

    switch (log->d.m_level)
    {
        case Log::eInfo:
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::eInfo, log->d.m_tag, log->d.m_log);
            break;
        case Log::eWarn:
            if (m_showFileWarningLog.load())
                itLogger->second->log(FileLogger::eWarn, log->d.m_tag, log->d.m_log);
            break;
        case Log::eError:
            if (m_showFileErrorLog.load())
                itLogger->second->log(FileLogger::eError, log->d.m_tag, log->d.m_log);
            break;
        case Log::eDebug:
            if (m_showFileDebugLog.load())
                itLogger->second->log(FileLogger::eDebug, log->d.m_tag, log->d.m_log);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, use Info");
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::eInfo, log->d.m_tag, log->d.m_log);
            break;
    }

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::writeFileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    auto fileLogger = m_fileLoggers.find(str->d);
    if (fileLogger == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("logger: " + str->d + " not found.");
        return YomkResponse(YomkResponse::eNo, "logger not found.");
    }
    fileLogger->second->write();

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::deleteLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yName);

    // 空名拒绝（同 createFileLogger 惯例）
    if (yName->d.empty())
    {
        YOMK_ERR_POS_LOG("logger name is empty.");
        return YomkResponse(YomkResponse::eInvalid, "logger name is empty.");
    }

    // 分段持锁不嵌套：删除无需跨表原子快照，同时持两把写锁会阻塞全部日志写入
    size_t deletedConsole = 0;
    {
        std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        deletedConsole = m_consoleLoggers.erase(yName->d);
    }

    // 锁内拷出、锁外析构：~FileLogger 落盘 I/O 不阻塞日志写入
    size_t deletedFile = 0;
    FileLoggerPtr removedFile;
    {
        std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(yName->d);
        if (itLogger != m_fileLoggers.end())
        {
            removedFile = itLogger->second;
            m_fileLoggers.erase(itLogger);
            deletedFile = 1;
        }
    }

    if (deletedConsole == 0 && deletedFile == 0)
    {
        YOMK_ERR_POS_LOG("logger: " + yName->d + " not found.");
        return YomkResponse(YomkResponse::eNo, "logger not found.");
    }

    // 不删除磁盘上的 .log 文件（清理归调用方）
    return YomkResponse(
        YomkResponse::eOk,
        "deleted console:" + std::to_string(deletedConsole) + " file:" + std::to_string(deletedFile));
}

YomkResponse YomkLogger::offConsoleLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedConsoleLevel(log->d.m_logger, log->d.m_level, false);
    }

    // 空名 = 全局主闸路径（与既有语义一致）
    switch (log->d.m_level)
    {
        case Log::eInfo:
            m_showConsoleInfoLog.store(false);
            break;
        case Log::eWarn:
            m_showConsoleWarningLog.store(false);
            break;
        case Log::eError:
            m_showConsoleErrorLog.store(false);
            break;
        case Log::eDebug:
            m_showConsoleDebugLog.store(false);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn off failed.");
            break;
    }

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::onConsoleLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedConsoleLevel(log->d.m_logger, log->d.m_level, true);
    }

    // 空名 = 全局主闸路径（与既有语义一致）
    switch (log->d.m_level)
    {
        case Log::eInfo:
            m_showConsoleInfoLog.store(true);
            break;
        case Log::eWarn:
            m_showConsoleWarningLog.store(true);
            break;
        case Log::eError:
            m_showConsoleErrorLog.store(true);
            break;
        case Log::eDebug:
            m_showConsoleDebugLog.store(true);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn on failed.");
            break;
    }
    return YomkResponse(YomkResponse::eOk, "success.");
}

// 具名开关唯一写入口：表读锁内查找拷指针出锁、锁外 setLevelOn（单锁；未命中 eNo 不惰性创建）
YomkResponse YomkLogger::setNamedConsoleLevel(const std::string& loggerName, Log::ELogLevel level, bool on)
{
    ConsoleLoggerPtr consoleLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(loggerName);
        if (itLogger == m_consoleLoggers.end())
        {
            YOMK_ERR_POS_LOG("console logger: " + loggerName + " not found.");
            return YomkResponse(YomkResponse::eNo, "console logger not found.");
        }
        consoleLogger = itLogger->second;
    }

    switch (level)
    {
        case Log::eInfo:
            consoleLogger->setLevelOn(ConsoleLogger::eInfo, on);
            break;
        case Log::eWarn:
            consoleLogger->setLevelOn(ConsoleLogger::eWarn, on);
            break;
        case Log::eError:
            consoleLogger->setLevelOn(ConsoleLogger::eError, on);
            break;
        case Log::eDebug:
            consoleLogger->setLevelOn(ConsoleLogger::eDebug, on);
            break;
        default:
            YOMK_ERR_POS_LOG(std::string("unknown log level, turn ") + (on ? "on" : "off") + " failed.");
            break;
    }
    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::offFileLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedFileLevel(log->d.m_logger, log->d.m_level, false);
    }

    // 空名 = 全局主闸路径（与控制台侧双模式语义对称）
    switch (log->d.m_level)
    {
        case Log::eInfo:
            m_showFileInfoLog.store(false);
            break;
        case Log::eWarn:
            m_showFileWarningLog.store(false);
            break;
        case Log::eError:
            m_showFileErrorLog.store(false);
            break;
        case Log::eDebug:
            m_showFileDebugLog.store(false);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn off failed.");
            break;
    }

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::onFileLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 eNo 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedFileLevel(log->d.m_logger, log->d.m_level, true);
    }

    // 空名 = 全局主闸路径（与控制台侧双模式语义对称）
    switch (log->d.m_level)
    {
        case Log::eInfo:
            m_showFileInfoLog.store(true);
            break;
        case Log::eWarn:
            m_showFileWarningLog.store(true);
            break;
        case Log::eError:
            m_showFileErrorLog.store(true);
            break;
        case Log::eDebug:
            m_showFileDebugLog.store(true);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn on failed.");
            break;
    }
    return YomkResponse(YomkResponse::eOk, "success.");
}

// 文件侧具名开关唯一写入口：表读锁内查找拷指针出锁、锁外 setLevelOn（单锁；未命中 eNo 不惰性创建）
YomkResponse YomkLogger::setNamedFileLevel(const std::string& loggerName, Log::ELogLevel level, bool on)
{
    FileLoggerPtr fileLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(loggerName);
        if (itLogger == m_fileLoggers.end())
        {
            YOMK_ERR_POS_LOG("file logger: " + loggerName + " not found.");
            return YomkResponse(YomkResponse::eNo, "file logger not found.");
        }
        fileLogger = itLogger->second;
    }

    switch (level)
    {
        case Log::eInfo:
            fileLogger->setLevelOn(FileLogger::eInfo, on);
            break;
        case Log::eWarn:
            fileLogger->setLevelOn(FileLogger::eWarn, on);
            break;
        case Log::eError:
            fileLogger->setLevelOn(FileLogger::eError, on);
            break;
        case Log::eDebug:
            fileLogger->setLevelOn(FileLogger::eDebug, on);
            break;
        default:
            YOMK_ERR_POS_LOG(std::string("unknown log level, turn ") + (on ? "on" : "off") + " failed.");
            break;
    }
    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::loggers(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, type);
    std::vector<std::string> names;
    if (type->d == "console")
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        names.reserve(m_consoleLoggers.size());
        for (auto& iter : m_consoleLoggers)
            names.push_back(iter.first);
    }
    else if (type->d == "file")
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        names.reserve(m_fileLoggers.size());
        for (auto& iter : m_fileLoggers)
            names.push_back(iter.first);
    }
    else
    {
        YOMK_ERR_POS_LOG("unknown logger type: " + type->d + ".");
        return YomkResponse(YomkResponse::eNo, "unknown logger type.");
    }
    // 单表为 std::map，中序遍历天然字典序，无需排序（选型见 YomkLogger.h）
    // move 入包消除全量拷贝
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, std::move(names))};
}

YomkResponse YomkLogger::loggerInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, LoggerQuery, query);
    // 按类型单表精确查找：两表允许同名，不再先 console 后 file 双表 fallback（同名歧义由 type 消除）；
    // 命中：锁内构造结构化快照，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）
    yomk::LoggerInfo info;
    info.m_loggerName = query->d.m_loggerName;
    if (query->d.m_type == "console")
    {
        // 锁内查找拷指针出锁外使用（与 consoleLog 双检锁同款模式）
        ConsoleLoggerPtr consoleLogger;
        {
            std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
            auto itLogger = m_consoleLoggers.find(query->d.m_loggerName);
            if (itLogger == m_consoleLoggers.end())
            {
                YOMK_ERR_POS_LOG("console logger: " + query->d.m_loggerName + " not found.");
                return YomkResponse(YomkResponse::eNo, "logger not found.");
            }
            consoleLogger = itLogger->second;
        }
        // 状态段：条目报逐器门控层真值；proxyOn 在表锁外独立取（proxy 叶子锁独立作用域）
        bool proxyOn = false;
        {
            std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
            proxyOn = m_consoleLogProxy;
        }
        info.m_type = "console";
        info.m_loggerState = namedConsoleState(consoleLogger, proxyOn);
        return {YomkResponse::eOk, "ok", YomkMkPtr(LoggerInfo, std::move(info))};
    }
    if (query->d.m_type == "file")
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(query->d.m_loggerName);
        if (itLogger == m_fileLoggers.end())
        {
            YOMK_ERR_POS_LOG("file logger: " + query->d.m_loggerName + " not found.");
            return YomkResponse(YomkResponse::eNo, "logger not found.");
        }
        info.m_type = "file";
        info.m_dir = itLogger->second->getDir();
        // 状态段：条目报文件逐器门控层真值；proxyOn 为文件代理态（闭环 3 落地前恒 false）
        info.m_loggerState = namedFileState(itLogger->second, false);
        return {YomkResponse::eOk, "ok", YomkMkPtr(LoggerInfo, std::move(info))};
    }
    YOMK_ERR_POS_LOG("unknown logger type: " + query->d.m_type + ".");
    return YomkResponse(YomkResponse::eNo, "unknown logger type.");
}

// 取控制台全局主闸态（/console_global_state 返回体）：四个级别开关为独立 atomic（组合本无原子快照保证），
// proxy 叶子锁独立作用域
LoggerState YomkLogger::currentConsoleState()
{
    yomk::LoggerState state;
    state.m_debugOn = m_showConsoleDebugLog.load();
    state.m_infoOn = m_showConsoleInfoLog.load();
    state.m_warnOn = m_showConsoleWarningLog.load();
    state.m_errorOn = m_showConsoleErrorLog.load();
    {
        // proxy 状态读侧与写侧互斥（叶子锁）
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        state.m_proxyOn = m_consoleLogProxy;
    }
    return state;
}

// 取单条 console 条目状态：四级别为逐器门控层真值（logger.isLevelOn），proxyOn 由调用点传入
LoggerState YomkLogger::namedConsoleState(const ConsoleLoggerPtr& consoleLogger, bool proxyOn)
{
    yomk::LoggerState state;
    state.m_debugOn = consoleLogger->isLevelOn(ConsoleLogger::eDebug);
    state.m_infoOn = consoleLogger->isLevelOn(ConsoleLogger::eInfo);
    state.m_warnOn = consoleLogger->isLevelOn(ConsoleLogger::eWarn);
    state.m_errorOn = consoleLogger->isLevelOn(ConsoleLogger::eError);
    state.m_proxyOn = proxyOn;
    return state;
}

// 取文件全局主闸态（/file_global_state 返回体）：四个级别开关独立 atomic（与控制台主闸相互独立）；
// proxyOn 为文件代理态（闭环 3 接管，落地前恒 false）
LoggerState YomkLogger::currentFileState()
{
    yomk::LoggerState state;
    state.m_debugOn = m_showFileDebugLog.load();
    state.m_infoOn = m_showFileInfoLog.load();
    state.m_warnOn = m_showFileWarningLog.load();
    state.m_errorOn = m_showFileErrorLog.load();
    state.m_proxyOn = false;
    return state;
}

// 取单条 file 条目状态：四级别为逐器门控层真值（logger.isLevelOn），proxyOn 由调用点传入
LoggerState YomkLogger::namedFileState(const FileLoggerPtr& fileLogger, bool proxyOn)
{
    yomk::LoggerState state;
    state.m_debugOn = fileLogger->isLevelOn(FileLogger::eDebug);
    state.m_infoOn = fileLogger->isLevelOn(FileLogger::eInfo);
    state.m_warnOn = fileLogger->isLevelOn(FileLogger::eWarn);
    state.m_errorOn = fileLogger->isLevelOn(FileLogger::eError);
    state.m_proxyOn = proxyOn;
    return state;
}

YomkResponse YomkLogger::listAll(YomkPkgPtr pkg)
{
    (void)pkg;
    // LoggerState 内嵌每条 LoggerInfo：console 条目报控制台逐器门控层真值（namedConsoleState 逐条求值），
    // file 条目报文件逐器门控层真值（namedFileState 逐条求值，proxyOn 文件代理落地前恒 false）
    // console proxyOn 在双表锁外先独立取（proxy 叶子锁独立作用域），随后传入逐条求值
    bool proxyOn = false;
    {
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        proxyOn = m_consoleLogProxy;
    }
    std::vector<yomk::LoggerInfo> loggers;
    {
        // 按锁序嵌套双读锁取跨段快照，逐条构造 LoggerInfo（与 loggerInfo 命中分支同构）
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        loggers.reserve(m_consoleLoggers.size() + m_fileLoggers.size());
        for (auto& iter : m_consoleLoggers)
        {
            yomk::LoggerInfo info;
            info.m_loggerName = iter.first;
            info.m_type = "console";
            info.m_loggerState = namedConsoleState(iter.second, proxyOn);
            loggers.push_back(std::move(info));
        }
        for (auto& iter : m_fileLoggers)
        {
            yomk::LoggerInfo info;
            info.m_loggerName = iter.first;
            info.m_type = "file";
            info.m_dir = iter.second->getDir();
            info.m_loggerState = namedFileState(iter.second, false);
            loggers.push_back(std::move(info));
        }
    }
    // std::map 天然字典序（console 段在前、file 段在后）；m_msg 恒 "ok"，数据走 m_data
    return {YomkResponse::eOk, "ok", YomkMkPtr(LoggerInfoList, std::move(loggers))};
}

// 控制台全局主闸态内省（无参形态同 /all；m_msg 恒 "ok"，LoggerState 包走 m_data）
YomkResponse YomkLogger::consoleGlobalState(YomkPkgPtr pkg)
{
    (void)pkg;
    return {YomkResponse::eOk, "ok", YomkMkPtr(LoggerState, currentConsoleState())};
}

// 文件全局主闸态内省（无参形态同 /all；m_msg 恒 "ok"，LoggerState 包走 m_data）
YomkResponse YomkLogger::fileGlobalState(YomkPkgPtr pkg)
{
    (void)pkg;
    return {YomkResponse::eOk, "ok", YomkMkPtr(LoggerState, currentFileState())};
}
