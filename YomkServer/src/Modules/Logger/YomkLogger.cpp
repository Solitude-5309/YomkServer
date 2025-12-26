#include "YomkLogger.h"

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

YomkRespond YomkLogger::createConsoleLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);
    if(m_consoleLoggers.find(yStr->d) != m_consoleLoggers.end())
    {
        return YomkRespond(YomkRespond::eErr, "logger name already exists.");
    }
    m_consoleLoggers[yStr->d] = std::make_shared<ConsoleLogger>();
    m_consoleLoggers[yStr->d]->setName(yStr->d);
    return YomkRespond();
}

YomkRespond YomkLogger::consoleLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YLog", YLog, yLog)

    if(m_consoleLoggers.find(yLog->m_logger) == m_consoleLoggers.end())
    {
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
        break;
    }

    return {YomkRespond::eOk, "success."};
}

YomkRespond YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YLogFile", YLogFile, yLogFile);

    if(m_fileLoggers.find(yLogFile->m_logger) != m_fileLoggers.end())
    {
        return YomkRespond(YomkRespond::eErr, "logger name already exists.");
    }

    m_fileLoggers[yLogFile->m_logger] = std::make_shared<FileLogger>();
    m_fileLoggers[yLogFile->m_logger]->setName(yLogFile->m_logger);
    m_fileLoggers[yLogFile->m_logger]->setDir(yLogFile->m_dir);
    m_fileLoggers[yLogFile->m_logger]->init();

    return YomkRespond();
}

YomkRespond YomkLogger::fileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YLog", YLog, yLog)

    if(m_fileLoggers.find(yLog->m_logger) == m_fileLoggers.end())
    {
        return YomkRespond(YomkRespond::eErr, "logger not found.");
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
        break;
    }

    return YomkRespond(YomkRespond::eOk, "success.");
}

YomkRespond YomkLogger::writeFileLog(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);
    if(m_fileLoggers.find(yStr->d) == m_fileLoggers.end())
    {
        return YomkRespond(YomkRespond::eErr, "logger not found.");
    }
    m_fileLoggers[yStr->d]->write();
    return YomkRespond(YomkRespond::eOk, "success.");
}
