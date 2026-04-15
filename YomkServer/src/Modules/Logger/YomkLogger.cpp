#include "YomkLogger.h"
#include <iostream>

YomkLogger::YomkLogger(YomkServer *server)
    : YomkService(server)
{
    name("/YomkLogger");
    m_consoleLoggers["MainLogger"] = std::make_shared<ConsoleLogger>();
}

YomkLogger::~YomkLogger()
{
    
}

int YomkLogger::init()
{
    YomkInstallFunc("/create_console_logger", YomkLogger::createConsoleLogger);
    YomkInstallFunc("/console_log", YomkLogger::consoleLog);
    YomkInstallFunc("/create_file_logger", YomkLogger::createFileLogger);
    YomkInstallFunc("/file_log", YomkLogger::fileLog);
    YomkInstallFunc("/write_file_log", YomkLogger::writeFileLog);

    return 0;
}

YomkResponse YomkLogger::createConsoleLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YString is empty");
    }
    std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
    if(m_consoleLoggers.find(yStr->d) != m_consoleLoggers.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "logger name already exists, please check YString" << std::endl;
        return YomkResponse(YomkResponse::eErr, "logger name already exists.");
    }
    std::shared_ptr<ConsoleLogger> consoleLogger = std::make_shared<ConsoleLogger>();
    consoleLogger->setName(yStr->d);
    m_consoleLoggers[yStr->d] = consoleLogger;
    return YomkResponse();
}

YomkResponse YomkLogger::consoleLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YLog", YLog, yLog)
    if(!yLog)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YLog is empty, please check YLog" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YLog is empty");
    }
    std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
    
    if(m_consoleLoggers.find(yLog->m_logger) == m_consoleLoggers.end())
    {   
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "logger not found, use MainLogger" << std::endl;
        yLog->m_logger = "MainLogger";
    }

    switch (yLog->m_level)
    {
    case YLog::eInfo:
        m_consoleLoggers[yLog->m_logger]->log(ConsoleLogger::eInfo, yLog->m_log);
        break;
    case YLog::eWarn:
        m_consoleLoggers[yLog->m_logger]->log(ConsoleLogger::eWarn, yLog->m_log);
        break;
    case YLog::eError:
        m_consoleLoggers[yLog->m_logger]->log(ConsoleLogger::eError, yLog->m_log);
        break;
    case YLog::eDebug:
        m_consoleLoggers[yLog->m_logger]->log(ConsoleLogger::eDebug, yLog->m_log);
        break;
    default:
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "unknown log level, use Info" << std::endl;
        m_consoleLoggers[yLog->m_logger]->log(ConsoleLogger::eInfo, yLog->m_log);
        break;
    }

    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YLogFile", YLogFile, yLogFile);
    if(!yLogFile)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YLogFile is empty, please check YLogFile" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YLogFile is empty");
    }
    std::unique_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    if(m_fileLoggers.find(yLogFile->m_logger) != m_fileLoggers.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "logger name already exists, please check YLogFile.m_logger" << std::endl;
        return YomkResponse(YomkResponse::eErr, "logger name already exists.");
    }

    m_fileLoggers[yLogFile->m_logger] = std::make_shared<FileLogger>();
    m_fileLoggers[yLogFile->m_logger]->setName(yLogFile->m_logger);
    m_fileLoggers[yLogFile->m_logger]->setDir(yLogFile->m_dir);
    m_fileLoggers[yLogFile->m_logger]->init();

    return YomkResponse();
}

YomkResponse YomkLogger::fileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YLog", YLog, yLog)
    if(!yLog)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YLog is empty, please check YLog" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YLog is empty");
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);

    if(m_fileLoggers.find(yLog->m_logger) == m_fileLoggers.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "logger not found." << std::endl;
        return YomkResponse(YomkResponse::eErr, "logger not found.");
    }

    switch (yLog->m_level)
    {
    case YLog::eInfo:
        m_fileLoggers[yLog->m_logger]->log(FileLogger::eInfo, yLog->m_log);
        break;
    case YLog::eWarn:
        m_fileLoggers[yLog->m_logger]->log(FileLogger::eWarn, yLog->m_log);
        break;
    case YLog::eError:
        m_fileLoggers[yLog->m_logger]->log(FileLogger::eError, yLog->m_log);
        break;
    case YLog::eDebug:
        m_fileLoggers[yLog->m_logger]->log(FileLogger::eDebug, yLog->m_log);
        break;
    default:
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "unknown log level, use Info" << std::endl;
        m_fileLoggers[yLog->m_logger]->log(FileLogger::eInfo, yLog->m_log);
        break;
    }

    return YomkResponse(YomkResponse::eOk, "success.");
}

YomkResponse YomkLogger::writeFileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YString is empty");
    }
    std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
    auto fileLogger = m_fileLoggers.find(yStr->d);
    if(fileLogger == m_fileLoggers.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "logger not found." << std::endl;
        return YomkResponse(YomkResponse::eErr, "logger not found.");
    }
    fileLogger->second->write();
    
    return YomkResponse(YomkResponse::eOk, "success.");
}
