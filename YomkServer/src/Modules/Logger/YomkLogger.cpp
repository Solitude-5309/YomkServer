#include "YomkLogger.h"

#include <iostream>

// yomk 命名空间引入收口在 .cpp（头文件不得 using，防止与全局 ::EventLoop 撞名二义性外泄）
using namespace yomk::payload;

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
      m_consoleLogProxyFunc(nullptr),
      m_fileLogProxy(false),
      m_fileLogProxyFunc(nullptr)
{
    name("/YomkLogger");
    m_consoleLoggers["MainLogger"] = std::make_shared<ConsoleLogger>();
}

YomkLogger::~YomkLogger() {}

int YomkLogger::init()
{
    installFunc("/set_console_log_proxy", &YomkLogger::setConsoleLogProxy, "ConsoleLogProxy");
    installFunc("/set_file_log_proxy", &YomkLogger::setFileLogProxy, "FileLogProxy");
    installFunc("/console_log", &YomkLogger::consoleLog, "Log");
    installFunc("/create_file_logger", &YomkLogger::createFileLogger, "LogFile");
    installFunc("/file_log", &YomkLogger::fileLog, "Log");
    installFunc("/write_file_log", &YomkLogger::writeFileLog, "String");
    installFunc("/delete_logger", &YomkLogger::deleteLogger, "String");
    installFunc("/off_console_log_by_level", &YomkLogger::offConsoleLogByLevel, "Log");
    installFunc("/on_console_log_by_level", &YomkLogger::onConsoleLogByLevel, "Log");
    installFunc("/loggers", &YomkLogger::loggers, "String");
    installFunc("/logger", &YomkLogger::loggerInfo, "LoggerQuery");
    installFunc("/all", &YomkLogger::listAll);
    installFunc("/console_global_state", &YomkLogger::consoleGlobalState);
    installFunc("/off_file_log_by_level", &YomkLogger::offFileLogByLevel, "Log");
    installFunc("/on_file_log_by_level", &YomkLogger::onFileLogByLevel, "Log");
    installFunc("/file_global_state", &YomkLogger::fileGlobalState);
    return 0;
}

yomk::payload::Response YomkLogger::consoleLog(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }
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
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "console log proxy is success.");
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
        case LogLevel::Info:
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Info, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Warn:
            if (m_showConsoleWarningLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Warn, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Error:
            if (m_showConsoleErrorLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Error, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Debug:
            if (m_showConsoleDebugLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Debug, log->d.m_tag, log->d.m_log);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, use Info");
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Info, log->d.m_tag, log->d.m_log);
            break;
    }

    return {yomk::payload::ResStatus::Succeeded, "success."};
}

yomk::payload::Response YomkLogger::setConsoleLogProxy(YomkMsgPtr pkg)
{
    auto consoleLogProxy = yomk::msgs::unPack<yomk::msgs::ConsoleLogProxy>(pkg);
    if (!consoleLogProxy)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not ConsoleLogProxy"};
    }
    // 与 consoleLog 读侧互斥
    std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    // 传空回调即卸载 proxy、恢复默认输出；两字段同锁一致更新
    m_consoleLogProxy = (m_consoleLogProxyFunc != nullptr);
    return {yomk::payload::ResStatus::Succeeded, "success."};
}

yomk::payload::Response YomkLogger::setFileLogProxy(YomkMsgPtr pkg)
{
    auto fileLogProxy = yomk::msgs::unPack<yomk::msgs::FileLogProxy>(pkg);
    if (!fileLogProxy)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not FileLogProxy"};
    }
    // 与 fileLog 读侧互斥；语义与 setConsoleLogProxy 对称
    std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
    m_fileLogProxyFunc = fileLogProxy->d.m_fileLogProxyFunc;
    // 传空回调即卸载 proxy、恢复默认落盘；两字段同锁一致更新
    m_fileLogProxy = (m_fileLogProxyFunc != nullptr);
    return {yomk::payload::ResStatus::Succeeded, "success."};
}

yomk::payload::Response YomkLogger::createFileLogger(YomkMsgPtr pkg)
{
    auto logFile = yomk::msgs::unPack<yomk::msgs::LogFile>(pkg);
    if (!logFile)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not LogFile"};
    }

    // 空名/空目录拒绝：ResStatus::Invalid（参数无效惯例）
    if (logFile->d.m_logger.empty())
    {
        YOMK_ERR_POS_LOG("logger name is empty.");
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger name is empty.");
    }
    if (logFile->d.m_dir.empty())
    {
        YOMK_ERR_POS_LOG("logger dir is empty.");
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger dir is empty.");
    }

    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if (m_fileLoggers.find(logFile->d.m_logger) != m_fileLoggers.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger name already exists.");
    }

    std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
    fileLogger->setName(logFile->d.m_logger);
    fileLogger->setDir(logFile->d.m_dir);
    if (!fileLogger->init())
    {
        YOMK_ERR_POS_LOG("init file logger failed: " + logFile->d.m_logger);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "init file logger failed.");
    }
    m_fileLoggers.emplace(logFile->d.m_logger, fileLogger);

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::fileLog(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }

    if (log->d.m_logger.empty())
    {
        YOMK_ERR_POS_LOG("file logger name is empty.");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger name is empty.");
    }

    // 锁内拷贝文件代理快照，锁外调用回调（与 consoleLog 同款模式，避免用户回调持锁重入）；
    // 代理先于表查找、主闸与逐器门控（未知名在拦截态下同样被短路，语义与 console 一致）
    bool proxyEnabled = false;
    YomkFileLogProxyFunc proxyFunc;
    {
        std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
        proxyEnabled = m_fileLogProxy;
        proxyFunc = m_fileLogProxyFunc;
    }
    if (proxyEnabled && proxyFunc && !proxyFunc(log->d))
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "file log proxy is success.");
    }

    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);

    auto itLogger = m_fileLoggers.find(log->d.m_logger);
    if (itLogger == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("file logger: " + log->d.m_logger + " not found.");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger not found.");
    }

    switch (log->d.m_level)
    {
        case LogLevel::Info:
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::LogLevel::Info, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Warn:
            if (m_showFileWarningLog.load())
                itLogger->second->log(FileLogger::LogLevel::Warn, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Error:
            if (m_showFileErrorLog.load())
                itLogger->second->log(FileLogger::LogLevel::Error, log->d.m_tag, log->d.m_log);
            break;
        case LogLevel::Debug:
            if (m_showFileDebugLog.load())
                itLogger->second->log(FileLogger::LogLevel::Debug, log->d.m_tag, log->d.m_log);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, use Info");
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::LogLevel::Info, log->d.m_tag, log->d.m_log);
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::writeFileLog(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    auto fileLogger = m_fileLoggers.find(str->d);
    if (fileLogger == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("logger: " + str->d + " not found.");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger not found.");
    }
    fileLogger->second->write();

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::deleteLogger(YomkMsgPtr pkg)
{
    auto yName = yomk::msgs::unPack<yomk::msgs::String>(pkg);
    if (!yName)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }

    // 空名拒绝（同 createFileLogger 惯例）
    if (yName->d.empty())
    {
        YOMK_ERR_POS_LOG("logger name is empty.");
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger name is empty.");
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
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger not found.");
    }

    // 不删除磁盘上的 .log 文件（清理归调用方）
    return yomk::payload::Response(
        yomk::payload::ResStatus::Succeeded,
        "deleted console:" + std::to_string(deletedConsole) + " file:" + std::to_string(deletedFile));
}

yomk::payload::Response YomkLogger::offConsoleLogByLevel(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 ResStatus::Failed 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedConsoleLevel(log->d.m_logger, log->d.m_level, false);
    }

    // 空名 = 全局主闸路径（与既有语义一致）
    switch (log->d.m_level)
    {
        case LogLevel::Info:
            m_showConsoleInfoLog.store(false);
            break;
        case LogLevel::Warn:
            m_showConsoleWarningLog.store(false);
            break;
        case LogLevel::Error:
            m_showConsoleErrorLog.store(false);
            break;
        case LogLevel::Debug:
            m_showConsoleDebugLog.store(false);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn off failed.");
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::onConsoleLogByLevel(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 ResStatus::Failed 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedConsoleLevel(log->d.m_logger, log->d.m_level, true);
    }

    // 空名 = 全局主闸路径（与既有语义一致）
    switch (log->d.m_level)
    {
        case LogLevel::Info:
            m_showConsoleInfoLog.store(true);
            break;
        case LogLevel::Warn:
            m_showConsoleWarningLog.store(true);
            break;
        case LogLevel::Error:
            m_showConsoleErrorLog.store(true);
            break;
        case LogLevel::Debug:
            m_showConsoleDebugLog.store(true);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn on failed.");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

// 具名开关唯一写入口：表读锁内查找拷指针出锁、锁外 setLevelOn（单锁；未命中 ResStatus::Failed 不惰性创建）
yomk::payload::Response YomkLogger::setNamedConsoleLevel(const std::string& loggerName, LogLevel level, bool on)
{
    ConsoleLoggerPtr consoleLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(loggerName);
        if (itLogger == m_consoleLoggers.end())
        {
            YOMK_ERR_POS_LOG("console logger: " + loggerName + " not found.");
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "console logger not found.");
        }
        consoleLogger = itLogger->second;
    }

    switch (level)
    {
        case LogLevel::Info:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Info, on);
            break;
        case LogLevel::Warn:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Warn, on);
            break;
        case LogLevel::Error:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Error, on);
            break;
        case LogLevel::Debug:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Debug, on);
            break;
        default:
            YOMK_ERR_POS_LOG(std::string("unknown log level, turn ") + (on ? "on" : "off") + " failed.");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::offFileLogByLevel(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 ResStatus::Failed 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedFileLevel(log->d.m_logger, log->d.m_level, false);
    }

    // 空名 = 全局主闸路径（与控制台侧双模式语义对称）
    switch (log->d.m_level)
    {
        case LogLevel::Info:
            m_showFileInfoLog.store(false);
            break;
        case LogLevel::Warn:
            m_showFileWarningLog.store(false);
            break;
        case LogLevel::Error:
            m_showFileErrorLog.store(false);
            break;
        case LogLevel::Debug:
            m_showFileDebugLog.store(false);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn off failed.");
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::onFileLogByLevel(YomkMsgPtr pkg)
{
    auto log = yomk::msgs::unPack<yomk::msgs::Log>(pkg);
    if (!log)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Log"};
    }

    // 具名请求：逐器门控路径（写第二层，不触碰全局主闸；未命中 ResStatus::Failed 不惰性创建）
    if (!log->d.m_logger.empty())
    {
        return setNamedFileLevel(log->d.m_logger, log->d.m_level, true);
    }

    // 空名 = 全局主闸路径（与控制台侧双模式语义对称）
    switch (log->d.m_level)
    {
        case LogLevel::Info:
            m_showFileInfoLog.store(true);
            break;
        case LogLevel::Warn:
            m_showFileWarningLog.store(true);
            break;
        case LogLevel::Error:
            m_showFileErrorLog.store(true);
            break;
        case LogLevel::Debug:
            m_showFileDebugLog.store(true);
            break;
        default:
            YOMK_ERR_POS_LOG("unknown log level, turn on failed.");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

// 文件侧具名开关唯一写入口：表读锁内查找拷指针出锁、锁外 setLevelOn（单锁；未命中 ResStatus::Failed 不惰性创建）
yomk::payload::Response YomkLogger::setNamedFileLevel(const std::string& loggerName, LogLevel level, bool on)
{
    FileLoggerPtr fileLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(loggerName);
        if (itLogger == m_fileLoggers.end())
        {
            YOMK_ERR_POS_LOG("file logger: " + loggerName + " not found.");
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger not found.");
        }
        fileLogger = itLogger->second;
    }

    switch (level)
    {
        case LogLevel::Info:
            fileLogger->setLevelOn(FileLogger::LogLevel::Info, on);
            break;
        case LogLevel::Warn:
            fileLogger->setLevelOn(FileLogger::LogLevel::Warn, on);
            break;
        case LogLevel::Error:
            fileLogger->setLevelOn(FileLogger::LogLevel::Error, on);
            break;
        case LogLevel::Debug:
            fileLogger->setLevelOn(FileLogger::LogLevel::Debug, on);
            break;
        default:
            YOMK_ERR_POS_LOG(std::string("unknown log level, turn ") + (on ? "on" : "off") + " failed.");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success.");
}

yomk::payload::Response YomkLogger::loggers(YomkMsgPtr pkg)
{
    auto type = yomk::msgs::unPack<yomk::msgs::String>(pkg);
    if (!type)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
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
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type.");
    }
    // 单表为 std::map，中序遍历天然字典序，无需排序（选型见 YomkLogger.h）
    // move 入包消除全量拷贝
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(std::move(names))};
}

yomk::payload::Response YomkLogger::loggerInfo(YomkMsgPtr pkg)
{
    auto query = yomk::msgs::unPack<yomk::msgs::LoggerQuery>(pkg);
    if (!query)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not LoggerQuery"};
    }
    // 按类型单表精确查找：两表允许同名，不再先 console 后 file 双表 fallback（同名歧义由 type 消除）；
    // 命中：锁内构造结构化快照，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）
    yomk::payload::LoggerInfo info;
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
                return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger not found.");
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
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfo::shared(std::move(info))};
    }
    if (query->d.m_type == "file")
    {
        // 锁内查找拷指针出锁外使用（与 console 分支同款模式）
        FileLoggerPtr fileLogger;
        {
            std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
            auto itLogger = m_fileLoggers.find(query->d.m_loggerName);
            if (itLogger == m_fileLoggers.end())
            {
                YOMK_ERR_POS_LOG("file logger: " + query->d.m_loggerName + " not found.");
                return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger not found.");
            }
            fileLogger = itLogger->second;
        }
        // 状态段：条目报文件逐器门控层真值；proxyOn 在表锁外独立取（文件代理叶子锁独立作用域）
        bool proxyOn = false;
        {
            std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
            proxyOn = m_fileLogProxy;
        }
        info.m_type = "file";
        info.m_dir = fileLogger->getDir();
        info.m_loggerState = namedFileState(fileLogger, proxyOn);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfo::shared(std::move(info))};
    }
    YOMK_ERR_POS_LOG("unknown logger type: " + query->d.m_type + ".");
    return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type.");
}

// 取控制台全局主闸态（/console_global_state 返回体）：四个级别开关为独立 atomic（组合本无原子快照保证），
// proxy 叶子锁独立作用域
LoggerState YomkLogger::currentConsoleState()
{
    yomk::payload::LoggerState state;
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
    yomk::payload::LoggerState state;
    state.m_debugOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Debug);
    state.m_infoOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Info);
    state.m_warnOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Warn);
    state.m_errorOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Error);
    state.m_proxyOn = proxyOn;
    return state;
}

// 取文件全局主闸态（/file_global_state 返回体）：四个级别开关独立 atomic（与控制台主闸相互独立）；
// 文件代理态读侧与写侧互斥（叶子锁独立作用域，与控制台 proxyOn 各自独立）
LoggerState YomkLogger::currentFileState()
{
    yomk::payload::LoggerState state;
    state.m_debugOn = m_showFileDebugLog.load();
    state.m_infoOn = m_showFileInfoLog.load();
    state.m_warnOn = m_showFileWarningLog.load();
    state.m_errorOn = m_showFileErrorLog.load();
    {
        // proxy 状态读侧与写侧互斥（叶子锁）
        std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
        state.m_proxyOn = m_fileLogProxy;
    }
    return state;
}

// 取单条 file 条目状态：四级别为逐器门控层真值（logger.isLevelOn），proxyOn 由调用点传入
LoggerState YomkLogger::namedFileState(const FileLoggerPtr& fileLogger, bool proxyOn)
{
    yomk::payload::LoggerState state;
    state.m_debugOn = fileLogger->isLevelOn(FileLogger::LogLevel::Debug);
    state.m_infoOn = fileLogger->isLevelOn(FileLogger::LogLevel::Info);
    state.m_warnOn = fileLogger->isLevelOn(FileLogger::LogLevel::Warn);
    state.m_errorOn = fileLogger->isLevelOn(FileLogger::LogLevel::Error);
    state.m_proxyOn = proxyOn;
    return state;
}

yomk::payload::Response YomkLogger::listAll(YomkMsgPtr pkg)
{
    (void)pkg;
    // LoggerState 内嵌每条 LoggerInfo：console 条目报控制台逐器门控层真值（namedConsoleState 逐条求值），
    // file 条目报文件逐器门控层真值（namedFileState 逐条求值）
    // console/file 两个 proxyOn 各自在双表锁外独立取（两把 proxy 叶子锁独立作用域），随后传入逐条求值
    bool proxyOn = false;
    {
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        proxyOn = m_consoleLogProxy;
    }
    bool fileProxyOn = false;
    {
        std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
        fileProxyOn = m_fileLogProxy;
    }
    std::vector<yomk::payload::LoggerInfo> loggers;
    {
        // 按锁序嵌套双读锁取跨段快照，逐条构造 LoggerInfo（与 loggerInfo 命中分支同构）
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        loggers.reserve(m_consoleLoggers.size() + m_fileLoggers.size());
        for (auto& iter : m_consoleLoggers)
        {
            yomk::payload::LoggerInfo info;
            info.m_loggerName = iter.first;
            info.m_type = "console";
            info.m_loggerState = namedConsoleState(iter.second, proxyOn);
            loggers.push_back(std::move(info));
        }
        for (auto& iter : m_fileLoggers)
        {
            yomk::payload::LoggerInfo info;
            info.m_loggerName = iter.first;
            info.m_type = "file";
            info.m_dir = iter.second->getDir();
            info.m_loggerState = namedFileState(iter.second, fileProxyOn);
            loggers.push_back(std::move(info));
        }
    }
    // std::map 天然字典序（console 段在前、file 段在后）；m_msg 恒 "ok"，数据走 m_data
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfoList::shared(std::move(loggers))};
}

// 控制台全局主闸态内省（无参形态同 /all；m_msg 恒 "ok"，LoggerState 包走 m_data）
yomk::payload::Response YomkLogger::consoleGlobalState(YomkMsgPtr pkg)
{
    (void)pkg;
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerState::shared(currentConsoleState())};
}

// 文件全局主闸态内省（无参形态同 /all；m_msg 恒 "ok"，LoggerState 包走 m_data）
yomk::payload::Response YomkLogger::fileGlobalState(YomkMsgPtr pkg)
{
    (void)pkg;
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerState::shared(currentFileState())};
}
