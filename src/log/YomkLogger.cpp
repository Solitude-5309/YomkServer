#include "YomkLogger.h"
#include "yomk/log/YomkLoggerUrl.h"

#include <iostream>
#include "yomk/YomkDiag.h"
#include "yomk/log/YomkLoggerMsg.h"

// yomk 命名空间引入收口在 .cpp(头文件不得 using,防止与全局 ::EventLoop 撞名二义性外泄)
using namespace yomk::payload;

// 初始化列表须按 YomkLogger.h 声明序书写(-Wreorder)

YomkLogger::YomkLogger(YomkServer* server)
    : YomkService(server),
      m_showConsoleTraceLog(true),
      m_showConsoleDebugLog(true),
      m_showConsoleInfoLog(true),
      m_showConsoleWarningLog(true),
      m_showConsoleErrorLog(true),
      m_showConsoleFatalLog(true),
      m_showFileTraceLog(true),
      m_showFileDebugLog(true),
      m_showFileInfoLog(true),
      m_showFileWarningLog(true),
      m_showFileErrorLog(true),
      m_showFileFatalLog(true),
      m_consoleLogProxy(false),
      m_consoleLogProxyFunc(nullptr),
      m_fileLogProxy(false),
      m_fileLogProxyFunc(nullptr)
{
    name("/YomkLogger");
    m_consoleLoggers["main"] = std::make_shared<ConsoleLogger>();
}

YomkLogger::~YomkLogger() {}

int YomkLogger::init()
{
    installFunc("/set_console_log_proxy", &YomkLogger::setConsoleLogProxy, yomk::msgs::ConsoleLogProxy::kTypeName, yomk::url::kLoggerSetConsoleProxyDesc);
    installFunc("/set_file_log_proxy", &YomkLogger::setFileLogProxy, yomk::msgs::FileLogProxy::kTypeName, yomk::url::kLoggerSetFileLogProxyDesc);
    installFunc("/console_log", &YomkLogger::consoleLog, yomk::msgs::Log::kTypeName, yomk::url::kLoggerConsoleLogDesc);
    installFunc("/create_file_logger", &YomkLogger::createFileLogger, yomk::msgs::LogFile::kTypeName, yomk::url::kLoggerCreateFileLoggerDesc);
    installFunc("/file_log", &YomkLogger::fileLog, yomk::msgs::Log::kTypeName, yomk::url::kLoggerFileLogDesc);
    installFunc("/write_file_log", &YomkLogger::writeFileLog, yomk::msgs::String::kTypeName, yomk::url::kLoggerWriteFileLogDesc);
    installFunc("/delete_logger", &YomkLogger::deleteLogger, yomk::msgs::String::kTypeName, yomk::url::kLoggerDeleteLoggerDesc);
    installFunc("/off_console_log_by_level", &YomkLogger::offConsoleLogByLevel, yomk::msgs::Log::kTypeName, yomk::url::kLoggerOffConsoleLogByLevelDesc);
    installFunc("/on_console_log_by_level", &YomkLogger::onConsoleLogByLevel, yomk::msgs::Log::kTypeName, yomk::url::kLoggerOnConsoleLogByLevelDesc);
    installFunc("/loggers", &YomkLogger::loggers, yomk::msgs::String::kTypeName, yomk::url::kLoggerLoggersDesc);
    installFunc("/logger", &YomkLogger::loggerInfo, yomk::msgs::LoggerQuery::kTypeName, yomk::url::kLoggerLoggerDesc);
    installFunc("/all", &YomkLogger::listAll, "", yomk::url::kLoggerAllDesc);
    installFunc("/console_global_state", &YomkLogger::consoleGlobalState, "", yomk::url::kLoggerConsoleGlobalStateDesc);
    installFunc("/off_file_log_by_level", &YomkLogger::offFileLogByLevel, yomk::msgs::Log::kTypeName, yomk::url::kLoggerOffFileLogByLevelDesc);
    installFunc("/on_file_log_by_level", &YomkLogger::onFileLogByLevel, yomk::msgs::Log::kTypeName, yomk::url::kLoggerOnFileLogByLevelDesc);
    installFunc("/file_global_state", &YomkLogger::fileGlobalState, "", yomk::url::kLoggerFileGlobalStateDesc);
    return 0;
}

yomk::payload::Response YomkLogger::consoleLog(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }
    // 请求消息只读：需要补默认值时先拷出值副本，再在副本上改写
    yomk::payload::Log logData = logMsg->d;
    if (logData.logger.empty())
    {
        YOMK_DIAG_WARN("console logger name is empty, use main");
        logData.logger = "main";
    }

    // 锁内拷贝 proxy 快照,锁外调用回调(避免用户回调持锁重入)
    bool proxyEnabled = false;
    YomkConsoleLogProxyFunc proxyFunc;
    {
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        proxyEnabled = m_consoleLogProxy;
        proxyFunc = m_consoleLogProxyFunc;
    }
    if (proxyEnabled && proxyFunc && !proxyFunc(logData))
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
    }

    // 双检锁:读锁 miss 后升级写锁二次查找,logger 指针拷出锁外使用
    ConsoleLoggerPtr consoleLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(logData.logger);
        if (itLogger != m_consoleLoggers.end())
        {
            consoleLogger = itLogger->second;
        }
    }
    if (!consoleLogger)
    {
        std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(logData.logger);
        if (itLogger == m_consoleLoggers.end())
        {
            std::shared_ptr<ConsoleLogger> newLogger = std::make_shared<ConsoleLogger>();
            newLogger->setName(logData.logger);
            itLogger = m_consoleLoggers.emplace(logData.logger, newLogger).first;
        }
        consoleLogger = itLogger->second;
    }

    switch (logData.level)
    {
        case LogLevel::Trace:
            if (m_showConsoleTraceLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Trace, logData.tag, logData.log);
            break;
        case LogLevel::Info:
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Info, logData.tag, logData.log);
            break;
        case LogLevel::Warn:
            if (m_showConsoleWarningLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Warn, logData.tag, logData.log);
            break;
        case LogLevel::Error:
            if (m_showConsoleErrorLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Error, logData.tag, logData.log);
            break;
        case LogLevel::Debug:
            if (m_showConsoleDebugLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Debug, logData.tag, logData.log);
            break;
        case LogLevel::Fatal:
            if (m_showConsoleFatalLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Fatal, logData.tag, logData.log);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logData.level)) + ", fallback to Info");
            if (m_showConsoleInfoLog.load())
                consoleLogger->log(ConsoleLogger::LogLevel::Info, logData.tag, logData.log);
            break;
    }

    return {yomk::payload::ResStatus::Succeeded, "success"};
}

yomk::payload::Response YomkLogger::setConsoleLogProxy(YomkConstMsgPtr msg)
{
    auto consoleLogProxy = yomk::msgs::unpack<yomk::msgs::ConsoleLogProxy>(msg);
    if (!consoleLogProxy)
    {
        return {yomk::payload::ResStatus::Failed, consoleLogProxy.error()};
    }
    // 与 consoleLog 读侧互斥
    std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
    m_consoleLogProxyFunc = consoleLogProxy->d.consoleLogProxyFunc;
    // 传空回调即卸载 proxy,恢复默认输出;两字段同锁一致更新
    m_consoleLogProxy = (m_consoleLogProxyFunc != nullptr);
    return {yomk::payload::ResStatus::Succeeded, "success"};
}

yomk::payload::Response YomkLogger::setFileLogProxy(YomkConstMsgPtr msg)
{
    auto fileLogProxy = yomk::msgs::unpack<yomk::msgs::FileLogProxy>(msg);
    if (!fileLogProxy)
    {
        return {yomk::payload::ResStatus::Failed, fileLogProxy.error()};
    }
    // 与 fileLog 读侧互斥;语义与 setConsoleLogProxy 对称
    std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
    m_fileLogProxyFunc = fileLogProxy->d.fileLogProxyFunc;
    // 传空回调即卸载 proxy,恢复默认落盘;两字段同锁一致更新
    m_fileLogProxy = (m_fileLogProxyFunc != nullptr);
    return {yomk::payload::ResStatus::Succeeded, "success"};
}

yomk::payload::Response YomkLogger::createFileLogger(YomkConstMsgPtr msg)
{
    auto logFile = yomk::msgs::unpack<yomk::msgs::LogFile>(msg);
    if (!logFile)
    {
        return {yomk::payload::ResStatus::Failed, logFile.error()};
    }

    // 空名/空目录拒绝:ResStatus::Invalid(参数无效惯例)
    if (logFile->d.logger.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger name is empty");
    }
    if (logFile->d.dir.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger dir is empty");
    }

    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if (m_fileLoggers.find(logFile->d.logger) != m_fileLoggers.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger '" + logFile->d.logger + "' already exists");
    }

    std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
    fileLogger->setName(logFile->d.logger);
    fileLogger->setDir(logFile->d.dir);
    if (!fileLogger->init())
    {
        YOMK_DIAG_ERROR("init file logger '" + logFile->d.logger + "' failed");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "init file logger '" + logFile->d.logger + "' failed");
    }
    m_fileLoggers.emplace(logFile->d.logger, fileLogger);

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::fileLog(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }

    if (logMsg->d.logger.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger name is empty");
    }

    // 锁内拷贝文件代理快照,锁外调用回调(与 consoleLog 同款模式,避免用户回调持锁重入);
    // 代理先于表查找,主闸与逐器门控(未知名在拦截态下同样被短路,语义与 console 一致)
    bool proxyEnabled = false;
    YomkFileLogProxyFunc proxyFunc;
    {
        std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
        proxyEnabled = m_fileLogProxy;
        proxyFunc = m_fileLogProxyFunc;
    }
    if (proxyEnabled && proxyFunc && !proxyFunc(logMsg->d))
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
    }

    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);

    auto itLogger = m_fileLoggers.find(logMsg->d.logger);
    if (itLogger == m_fileLoggers.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger '" + logMsg->d.logger + "' not found");
    }

    switch (logMsg->d.level)
    {
        case LogLevel::Trace:
            if (m_showFileTraceLog.load())
                itLogger->second->log(FileLogger::LogLevel::Trace, logMsg->d.tag, logMsg->d.log);
            break;
        case LogLevel::Info:
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::LogLevel::Info, logMsg->d.tag, logMsg->d.log);
            break;
        case LogLevel::Warn:
            if (m_showFileWarningLog.load())
                itLogger->second->log(FileLogger::LogLevel::Warn, logMsg->d.tag, logMsg->d.log);
            break;
        case LogLevel::Error:
            if (m_showFileErrorLog.load())
                itLogger->second->log(FileLogger::LogLevel::Error, logMsg->d.tag, logMsg->d.log);
            break;
        case LogLevel::Debug:
            if (m_showFileDebugLog.load())
                itLogger->second->log(FileLogger::LogLevel::Debug, logMsg->d.tag, logMsg->d.log);
            break;
        case LogLevel::Fatal:
            if (m_showFileFatalLog.load())
                itLogger->second->log(FileLogger::LogLevel::Fatal, logMsg->d.tag, logMsg->d.log);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logMsg->d.level)) + ", fallback to Info");
            if (m_showFileInfoLog.load())
                itLogger->second->log(FileLogger::LogLevel::Info, logMsg->d.tag, logMsg->d.log);
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::writeFileLog(YomkConstMsgPtr msg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(msg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, str.error()};
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    auto fileLogger = m_fileLoggers.find(str->d);
    if (fileLogger == m_fileLoggers.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger '" + str->d + "' not found");
    }
    fileLogger->second->write();

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::deleteLogger(YomkConstMsgPtr msg)
{
    auto yName = yomk::msgs::unpack<yomk::msgs::String>(msg);
    if (!yName)
    {
        return {yomk::payload::ResStatus::Failed, yName.error()};
    }

    // 空名拒绝(同 createFileLogger 惯例)
    if (yName->d.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "logger name is empty");
    }

    // 分段持锁不嵌套:删除无需跨表原子快照,同时持两把写锁会阻塞全部日志写入
    size_t deletedConsole = 0;
    {
        std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        deletedConsole = m_consoleLoggers.erase(yName->d);
    }

    // 锁内拷出,锁外析构:~FileLogger 落盘 I/O 不阻塞日志写入
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
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "logger '" + yName->d + "' not found");
    }

    // 不删除磁盘上的 .log 文件(清理归调用方)
    return yomk::payload::Response(
        yomk::payload::ResStatus::Succeeded,
        "deleted console:" + std::to_string(deletedConsole) + " file:" + std::to_string(deletedFile));
}

yomk::payload::Response YomkLogger::offConsoleLogByLevel(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }

    // 具名请求:逐器门控路径(写第二层,不触碰全局主闸;未命中 ResStatus::Failed 不惰性创建)
    if (!logMsg->d.logger.empty())
    {
        return setNamedConsoleLevel(logMsg->d.logger, logMsg->d.level, false);
    }

    // 空名 = 全局主闸路径(与既有语义一致)
    switch (logMsg->d.level)
    {
        case LogLevel::Trace:
            m_showConsoleTraceLog.store(false);
            break;
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
        case LogLevel::Fatal:
            m_showConsoleFatalLog.store(false);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logMsg->d.level)) + ", turn off ignored");
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::onConsoleLogByLevel(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }

    // 具名请求:逐器门控路径(同 offConsoleLogByLevel)
    if (!logMsg->d.logger.empty())
    {
        return setNamedConsoleLevel(logMsg->d.logger, logMsg->d.level, true);
    }

    // 空名 = 全局主闸路径(同上)
    switch (logMsg->d.level)
    {
        case LogLevel::Trace:
            m_showConsoleTraceLog.store(true);
            break;
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
        case LogLevel::Fatal:
            m_showConsoleFatalLog.store(true);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logMsg->d.level)) + ", turn on ignored");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

// 具名开关唯一写入口:表读锁内查找拷指针出锁,锁外 setLevelOn(单锁;未命中 ResStatus::Failed 不惰性创建)
yomk::payload::Response YomkLogger::setNamedConsoleLevel(const std::string& loggerName, LogLevel level, bool on)
{
    ConsoleLoggerPtr consoleLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        auto itLogger = m_consoleLoggers.find(loggerName);
        if (itLogger == m_consoleLoggers.end())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "console logger '" + loggerName + "' not found");
        }
        consoleLogger = itLogger->second;
    }

    switch (level)
    {
        case LogLevel::Trace:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Trace, on);
            break;
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
        case LogLevel::Fatal:
            consoleLogger->setLevelOn(ConsoleLogger::LogLevel::Fatal, on);
            break;
        default:
            YOMK_DIAG_WARN(std::string("unknown log level ") + std::to_string(static_cast<int>(level)) + ", turn " + (on ? "on" : "off") + " ignored");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::offFileLogByLevel(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }

    // 具名请求:逐器门控路径(同 offConsoleLogByLevel)
    if (!logMsg->d.logger.empty())
    {
        return setNamedFileLevel(logMsg->d.logger, logMsg->d.level, false);
    }

    // 空名 = 全局主闸路径(与控制台侧对称)
    switch (logMsg->d.level)
    {
        case LogLevel::Trace:
            m_showFileTraceLog.store(false);
            break;
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
        case LogLevel::Fatal:
            m_showFileFatalLog.store(false);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logMsg->d.level)) + ", turn off ignored");
            break;
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::onFileLogByLevel(YomkConstMsgPtr msg)
{
    auto logMsg = yomk::msgs::unpack<yomk::msgs::Log>(msg);
    if (!logMsg)
    {
        return {yomk::payload::ResStatus::Failed, logMsg.error()};
    }

    // 具名请求:逐器门控路径(同 offConsoleLogByLevel)
    if (!logMsg->d.logger.empty())
    {
        return setNamedFileLevel(logMsg->d.logger, logMsg->d.level, true);
    }

    // 空名 = 全局主闸路径(与控制台侧对称)
    switch (logMsg->d.level)
    {
        case LogLevel::Trace:
            m_showFileTraceLog.store(true);
            break;
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
        case LogLevel::Fatal:
            m_showFileFatalLog.store(true);
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(static_cast<int>(logMsg->d.level)) + ", turn on ignored");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

// 文件侧具名开关唯一写入口:表读锁内查找拷指针出锁,锁外 setLevelOn(单锁;未命中 ResStatus::Failed 不惰性创建)
yomk::payload::Response YomkLogger::setNamedFileLevel(const std::string& loggerName, LogLevel level, bool on)
{
    FileLoggerPtr fileLogger;
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(loggerName);
        if (itLogger == m_fileLoggers.end())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger '" + loggerName + "' not found");
        }
        fileLogger = itLogger->second;
    }

    switch (level)
    {
        case LogLevel::Trace:
            fileLogger->setLevelOn(FileLogger::LogLevel::Trace, on);
            break;
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
        case LogLevel::Fatal:
            fileLogger->setLevelOn(FileLogger::LogLevel::Fatal, on);
            break;
        default:
            YOMK_DIAG_WARN(std::string("unknown log level ") + std::to_string(static_cast<int>(level)) + ", turn " + (on ? "on" : "off") + " ignored");
            break;
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "success");
}

yomk::payload::Response YomkLogger::loggers(YomkConstMsgPtr msg)
{
    auto type = yomk::msgs::unpack<yomk::msgs::String>(msg);
    if (!type)
    {
        return {yomk::payload::ResStatus::Failed, type.error()};
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
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type '" + type->d + "'");
    }
    // 单表为 std::map,中序遍历天然字典序,无需排序(选型见 YomkLogger.h)
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(std::move(names))};
}

yomk::payload::Response YomkLogger::loggerInfo(YomkConstMsgPtr msg)
{
    auto query = yomk::msgs::unpack<yomk::msgs::LoggerQuery>(msg);
    if (!query)
    {
        return {yomk::payload::ResStatus::Failed, query.error()};
    }
    // 按类型单表精确查找:两表允许同名,同名歧义由 type 消除;命中锁内构造结构化快照
    yomk::payload::LoggerInfo info;
    info.loggerName = query->d.loggerName;
    if (query->d.type == "console")
    {
        // 锁内查找拷指针出锁外使用(与 consoleLog 双检锁同款模式)
        ConsoleLoggerPtr consoleLogger;
        {
            std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
            auto itLogger = m_consoleLoggers.find(query->d.loggerName);
            if (itLogger == m_consoleLoggers.end())
            {
                return yomk::payload::Response(yomk::payload::ResStatus::Failed, "console logger '" + query->d.loggerName + "' not found");
            }
            consoleLogger = itLogger->second;
        }
        // 状态段:条目报逐器门控层真值;proxyOn 在表锁外独立取(proxy 叶子锁独立作用域)
        bool proxyOn = false;
        {
            std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
            proxyOn = m_consoleLogProxy;
        }
        info.type = "console";
        info.loggerState = namedConsoleState(consoleLogger, proxyOn);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfo::shared(std::move(info))};
    }
    if (query->d.type == "file")
    {
        // 锁内查找拷指针出锁外使用(与 console 分支同款模式)
        FileLoggerPtr fileLogger;
        {
            std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
            auto itLogger = m_fileLoggers.find(query->d.loggerName);
            if (itLogger == m_fileLoggers.end())
            {
                return yomk::payload::Response(yomk::payload::ResStatus::Failed, "file logger '" + query->d.loggerName + "' not found");
            }
            fileLogger = itLogger->second;
        }
        // 状态段:条目报文件逐器门控层真值;proxyOn 在表锁外独立取(文件代理叶子锁独立作用域)
        bool proxyOn = false;
        {
            std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
            proxyOn = m_fileLogProxy;
        }
        info.type = "file";
        info.dir = fileLogger->getDir();
        info.loggerState = namedFileState(fileLogger, proxyOn);
        return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfo::shared(std::move(info))};
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Failed, "unknown logger type '" + query->d.type + "'");
}

// 取控制台全局主闸态(/console_global_state 返回体):六级别开关为独立 atomic(组合本无原子快照保证),
// proxy 叶子锁独立作用域
LoggerState YomkLogger::currentConsoleState()
{
    yomk::payload::LoggerState state;
    state.traceOn = m_showConsoleTraceLog.load();
    state.debugOn = m_showConsoleDebugLog.load();
    state.infoOn = m_showConsoleInfoLog.load();
    state.warnOn = m_showConsoleWarningLog.load();
    state.errorOn = m_showConsoleErrorLog.load();
    state.fatalOn = m_showConsoleFatalLog.load();
    {
        // proxy 状态读侧与写侧互斥(叶子锁)
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        state.proxyOn = m_consoleLogProxy;
    }
    return state;
}

// 取单条 console 条目状态:六级别为逐器门控层真值(logger.isLevelOn),proxyOn 由调用点传入
LoggerState YomkLogger::namedConsoleState(const ConsoleLoggerPtr& consoleLogger, bool proxyOn)
{
    yomk::payload::LoggerState state;
    state.traceOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Trace);
    state.debugOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Debug);
    state.infoOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Info);
    state.warnOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Warn);
    state.errorOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Error);
    state.fatalOn = consoleLogger->isLevelOn(ConsoleLogger::LogLevel::Fatal);
    state.proxyOn = proxyOn;
    return state;
}

// 取文件全局主闸态(/file_global_state 返回体):六级别开关独立 atomic(与控制台主闸相互独立);
// 文件代理态读侧与写侧互斥(叶子锁独立作用域,与控制台 proxyOn 各自独立)
LoggerState YomkLogger::currentFileState()
{
    yomk::payload::LoggerState state;
    state.traceOn = m_showFileTraceLog.load();
    state.debugOn = m_showFileDebugLog.load();
    state.infoOn = m_showFileInfoLog.load();
    state.warnOn = m_showFileWarningLog.load();
    state.errorOn = m_showFileErrorLog.load();
    state.fatalOn = m_showFileFatalLog.load();
    {
        // proxy 状态读侧与写侧互斥(叶子锁)
        std::lock_guard<std::mutex> proxyLock(m_fileLogProxyMutex);
        state.proxyOn = m_fileLogProxy;
    }
    return state;
}

// 取单条 file 条目状态:六级别为逐器门控层真值(logger.isLevelOn),proxyOn 由调用点传入
LoggerState YomkLogger::namedFileState(const FileLoggerPtr& fileLogger, bool proxyOn)
{
    yomk::payload::LoggerState state;
    state.traceOn = fileLogger->isLevelOn(FileLogger::LogLevel::Trace);
    state.debugOn = fileLogger->isLevelOn(FileLogger::LogLevel::Debug);
    state.infoOn = fileLogger->isLevelOn(FileLogger::LogLevel::Info);
    state.warnOn = fileLogger->isLevelOn(FileLogger::LogLevel::Warn);
    state.errorOn = fileLogger->isLevelOn(FileLogger::LogLevel::Error);
    state.fatalOn = fileLogger->isLevelOn(FileLogger::LogLevel::Fatal);
    state.proxyOn = proxyOn;
    return state;
}

yomk::payload::Response YomkLogger::listAll(YomkConstMsgPtr msg)
{
    (void)msg;
    // 两个 proxyOn 各自在双表锁外独立取(两把 proxy 叶子锁独立作用域),再传入逐器门控层求值
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
        // 按锁序嵌套双读锁取跨段快照(与 loggerInfo 命中分支同构)
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        loggers.reserve(m_consoleLoggers.size() + m_fileLoggers.size());
        for (auto& iter : m_consoleLoggers)
        {
            yomk::payload::LoggerInfo info;
            info.loggerName = iter.first;
            info.type = "console";
            info.loggerState = namedConsoleState(iter.second, proxyOn);
            loggers.push_back(std::move(info));
        }
        for (auto& iter : m_fileLoggers)
        {
            yomk::payload::LoggerInfo info;
            info.loggerName = iter.first;
            info.type = "file";
            info.dir = iter.second->getDir();
            info.loggerState = namedFileState(iter.second, fileProxyOn);
            loggers.push_back(std::move(info));
        }
    }
    // std::map 天然字典序(console 段在前,file 段在后)
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerInfoList::shared(std::move(loggers))};
}

// 控制台全局主闸态内省(无参形态同 /all,LoggerState 包走 data)
yomk::payload::Response YomkLogger::consoleGlobalState(YomkConstMsgPtr msg)
{
    (void)msg;
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerState::shared(currentConsoleState())};
}

// 文件全局主闸态内省(无参形态同 /all,LoggerState 包走 data)
yomk::payload::Response YomkLogger::fileGlobalState(YomkConstMsgPtr msg)
{
    (void)msg;
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::LoggerState::shared(currentFileState())};
}
