#include "YomkLogger.h"
#include <iostream>

YomkLogger::YomkLogger(YomkServer *server)
    : YomkService(server)
{
    name("/YomkLogger");
    m_consoleLoggers["MainLogger"] = std::make_shared<ConsoleLogger>();
    m_showConsoleDebugLog.store(true);
    m_showConsoleInfoLog.store(true);
    m_showConsoleWarningLog.store(true);
    m_showConsoleErrorLog.store(true);
    m_consoleLogProxy = false;
    m_consoleLogProxyFunc = nullptr;
}

YomkLogger::~YomkLogger()
{
}

int YomkLogger::init()
{
    YomkInstallFunc("/set_console_log_proxy", YomkLogger::setConsoleLogProxy);
    YomkInstallFunc("/console_log", YomkLogger::consoleLog);
    YomkInstallFunc("/create_file_logger", YomkLogger::createFileLogger);
    YomkInstallFunc("/file_log", YomkLogger::fileLog);
    YomkInstallFunc("/write_file_log", YomkLogger::writeFileLog);
    YomkInstallFunc("/off_console_log_by_level", YomkLogger::offConsoleLogByLevel);
    YomkInstallFunc("/on_console_log_by_level", YomkLogger::onConsoleLogByLevel);
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

    if (m_consoleLogProxy && m_consoleLogProxyFunc && !m_consoleLogProxyFunc(log->d))
    {
        return YomkResponse(YomkResponse::eOk, "console log proxy is success.");
    }

    std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);

    auto itLogger = m_consoleLoggers.find(log->d.m_logger);
    if (itLogger == m_consoleLoggers.end())
    {
        std::shared_ptr<ConsoleLogger> consoleLogger = std::make_shared<ConsoleLogger>();
        consoleLogger->setName(log->d.m_logger);
        auto result = m_consoleLoggers.emplace(log->d.m_logger, consoleLogger);
        if (!result.second)
        {
            YOMK_ERR_POS_LOG("console logger: " + log->d.m_logger + " create failed.");
            return YomkResponse(YomkResponse::eNo, "console logger: " + log->d.m_logger + " create failed.");
        }
        itLogger = result.first;
    }

    switch (log->d.m_level)
    {
    case Log::eInfo:
        if (m_showConsoleInfoLog.load())
            itLogger->second->log(ConsoleLogger::eInfo, log->d.m_log);
        break;
    case Log::eWarn:
        if (m_showConsoleWarningLog.load())
            itLogger->second->log(ConsoleLogger::eWarn, log->d.m_log);
        break;
    case Log::eError:
        if (m_showConsoleErrorLog.load())
            itLogger->second->log(ConsoleLogger::eError, log->d.m_log);
        break;
    case Log::eDebug:
        if (m_showConsoleDebugLog.load())
            itLogger->second->log(ConsoleLogger::eDebug, log->d.m_log);
        break;
    default:
        YOMK_ERR_POS_LOG("unknown log level, use Info");
        if (m_showConsoleInfoLog.load())
            itLogger->second->log(ConsoleLogger::eInfo, log->d.m_log);
        break;
    }

    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::setConsoleLogProxy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ConsoleLogProxy, consoleLogProxy);
    m_consoleLogProxy = true;
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, LogFile, logFile);
    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if (m_fileLoggers.find(logFile->d.m_logger) != m_fileLoggers.end())
    {
        return YomkResponse(YomkResponse::eNo, "logger name already exists.");
    }

    std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
    fileLogger->setName(logFile->d.m_logger);
    fileLogger->setDir(logFile->d.m_dir);
    fileLogger->init();
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
        itLogger->second->log(FileLogger::eInfo, log->d.m_log);
        break;
    case Log::eWarn:
        itLogger->second->log(FileLogger::eWarn, log->d.m_log);
        break;
    case Log::eError:
        itLogger->second->log(FileLogger::eError, log->d.m_log);
        break;
    case Log::eDebug:
        itLogger->second->log(FileLogger::eDebug, log->d.m_log);
        break;
    default:
        YOMK_ERR_POS_LOG("unknown log level, use Info");
        itLogger->second->log(FileLogger::eInfo, log->d.m_log);
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

YomkResponse YomkLogger::offConsoleLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log);

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
