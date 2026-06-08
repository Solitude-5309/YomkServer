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
    YomkInstallFunc("/create_console_logger", YomkLogger::createConsoleLogger);
    YomkInstallFunc("/console_log", YomkLogger::consoleLog);
    YomkInstallFunc("/create_file_logger", YomkLogger::createFileLogger);
    YomkInstallFunc("/file_log", YomkLogger::fileLog);
    YomkInstallFunc("/write_file_log", YomkLogger::writeFileLog);
    YomkInstallFunc("/off_console_log_by_level", YomkLogger::offConsoleLogByLevel);
    YomkInstallFunc("/on_console_log_by_level", YomkLogger::onConsoleLogByLevel);
    return 0;
}

YomkResponse YomkLogger::createConsoleLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        YOMK_ERR_POS_LOG("string is empty, please check string");
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
    if(m_consoleLoggers.find(str->d) != m_consoleLoggers.end())
    {
        return YomkResponse(YomkResponse::eErr, "logger name already exists.");
    }
    std::shared_ptr<ConsoleLogger> consoleLogger = std::make_shared<ConsoleLogger>();
    consoleLogger->setName(str->d);
    m_consoleLoggers[str->d] = consoleLogger;
    return YomkResponse();
}

YomkResponse YomkLogger::consoleLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log)
    if(!log)
    {
        YOMK_ERR_POS_LOG("Log is empty, please check Log");
        return YomkResponse(YomkResponse::eErr, "Log is empty");
    }

    if(m_consoleLogProxy && m_consoleLogProxyFunc && !m_consoleLogProxyFunc(log->d))
    {
        return YomkResponse(YomkResponse::eOk, "console log proxy is success.");
    }

    std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
    
    if(m_consoleLoggers.find(log->d.m_logger) == m_consoleLoggers.end())
    {   
        YOMK_ERR_POS_LOG("logger: " + log->d.m_logger + " not found, use MainLogger");
        log->d.m_logger = "MainLogger";
    }

    switch (log->d.m_level)
    {
    case Log::eInfo:
        if(m_showConsoleInfoLog.load())
            m_consoleLoggers[log->d.m_logger]->log(ConsoleLogger::eInfo, log->d.m_log);
        break;
    case Log::eWarn:
        if(m_showConsoleWarningLog.load())
            m_consoleLoggers[log->d.m_logger]->log(ConsoleLogger::eWarn, log->d.m_log);
        break;
    case Log::eError:
        if(m_showConsoleErrorLog.load())
            m_consoleLoggers[log->d.m_logger]->log(ConsoleLogger::eError, log->d.m_log);
        break;
    case Log::eDebug:
        if(m_showConsoleDebugLog.load())
            m_consoleLoggers[log->d.m_logger]->log(ConsoleLogger::eDebug, log->d.m_log);
        break;
    default:
        YOMK_ERR_POS_LOG("unknown log level, use Info");
        if(m_showConsoleInfoLog.load())
            m_consoleLoggers[log->d.m_logger]->log(ConsoleLogger::eInfo, log->d.m_log);
        break;
    }

    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::setConsoleLogProxy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ConsoleLogProxy, consoleLogProxy);
    if(!consoleLogProxy)
    {
        YOMK_ERR_POS_LOG("ConsoleLogProxy is empty, please check ConsoleLogProxy");
        return YomkResponse(YomkResponse::eErr, "ConsoleLogProxy is empty");
    }
    m_consoleLogProxy = true;
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, LogFile, logFile);
    if(!logFile)
    {
        YOMK_ERR_POS_LOG("LogFile is empty, please check LogFile");
        return YomkResponse(YomkResponse::eErr, "LogFile is empty");
    }
    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if(m_fileLoggers.find(logFile->d.m_logger) != m_fileLoggers.end())
    {
        return YomkResponse(YomkResponse::eErr, "logger name already exists.");
    }

    m_fileLoggers[logFile->d.m_logger] = std::make_shared<FileLogger>();
    m_fileLoggers[logFile->d.m_logger]->setName(logFile->d.m_logger);
    m_fileLoggers[logFile->d.m_logger]->setDir(logFile->d.m_dir);
    m_fileLoggers[logFile->d.m_logger]->init();

    return YomkResponse();
}

YomkResponse YomkLogger::fileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log)
    if(!log)
    {
        YOMK_ERR_POS_LOG("Log is empty, please check Log");
        return YomkResponse(YomkResponse::eErr, "Log is empty");
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);

    if(m_fileLoggers.find(log->d.m_logger) == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("logger: " + log->d.m_logger + " not found.");
        return YomkResponse(YomkResponse::eErr, "logger not found.");
    }

    switch (log->d.m_level)
    {
    case Log::eInfo:
        m_fileLoggers[log->d.m_logger]->log(FileLogger::eInfo, log->d.m_log);
        break;
    case Log::eWarn:
        m_fileLoggers[log->d.m_logger]->log(FileLogger::eWarn, log->d.m_log);
        break;
    case Log::eError:
        m_fileLoggers[log->d.m_logger]->log(FileLogger::eError, log->d.m_log);
        break;
    case Log::eDebug:
        m_fileLoggers[log->d.m_logger]->log(FileLogger::eDebug, log->d.m_log);
        break;
    default:
        YOMK_ERR_POS_LOG("unknown log level, use Info");
        m_fileLoggers[log->d.m_logger]->log(FileLogger::eInfo, log->d.m_log);
        break;
    }

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::writeFileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        YOMK_ERR_POS_LOG("string is empty, please check string");
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    auto fileLogger = m_fileLoggers.find(str->d);
    if(fileLogger == m_fileLoggers.end())
    {
        YOMK_ERR_POS_LOG("logger: " + str->d + " not found.");
        return YomkResponse(YomkResponse::eErr, "logger not found.");
    }
    fileLogger->second->write();
    
    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::offConsoleLogByLevel(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Log, log)
    if(!log)
    {
        YOMK_ERR_POS_LOG("Log is empty, please check Log");
        return YomkResponse(YomkResponse::eErr, "Log is empty");
    }

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
    YomkUnPackPkgResponse(pkg, Log, log)
    if(!log)
    {
        YOMK_ERR_POS_LOG("Log is empty, please check Log");
        return YomkResponse(YomkResponse::eErr, "Log is empty");
    }

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
