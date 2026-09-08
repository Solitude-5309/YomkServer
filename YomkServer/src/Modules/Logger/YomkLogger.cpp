#include "YomkLogger.h"
#include <iostream>

// 初始化列表须按 YomkLogger.h 声明序书写（-Wreorder）
YomkLogger::YomkLogger(YomkServer *server)
    : YomkService(server), m_showConsoleDebugLog(true)
      ,
      m_showConsoleInfoLog(true), m_showConsoleWarningLog(true), m_showConsoleErrorLog(true), m_consoleLogProxy(false)
      ,
      m_consoleLogProxyFunc(nullptr)
{
    name("/YomkLogger");
    m_consoleLoggers["MainLogger"] = std::make_shared<ConsoleLogger>();
}

YomkLogger::~YomkLogger()
{
}

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
    YomkInstallFunc("/loggers", YomkLogger::loggers);
    YomkInstallFunc("/logger", YomkLogger::loggerInfo, String);
    YomkInstallFunc("/all", YomkLogger::listAll);
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
            consoleLogger->log(ConsoleLogger::eInfo, log->d.m_log);
        break;
    case Log::eWarn:
        if (m_showConsoleWarningLog.load())
            consoleLogger->log(ConsoleLogger::eWarn, log->d.m_log);
        break;
    case Log::eError:
        if (m_showConsoleErrorLog.load())
            consoleLogger->log(ConsoleLogger::eError, log->d.m_log);
        break;
    case Log::eDebug:
        if (m_showConsoleDebugLog.load())
            consoleLogger->log(ConsoleLogger::eDebug, log->d.m_log);
        break;
    default:
        YOMK_ERR_POS_LOG("unknown log level, use Info");
        if (m_showConsoleInfoLog.load())
            consoleLogger->log(ConsoleLogger::eInfo, log->d.m_log);
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
    return YomkResponse(YomkResponse::eOk,
                        "deleted console:" + std::to_string(deletedConsole) +
                            " file:" + std::to_string(deletedFile));
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

std::string YomkLogger::consoleLevelLine()
{
    std::string line = "console:debug:";
    line += m_showConsoleDebugLog.load() ? "on" : "off";
    line += " info:";
    line += m_showConsoleInfoLog.load() ? "on" : "off";
    line += " warn:";
    line += m_showConsoleWarningLog.load() ? "on" : "off";
    line += " error:";
    line += m_showConsoleErrorLog.load() ? "on" : "off";
    line += " proxy:";
    // 内省读侧与写侧互斥（叶子锁）
    {
        std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
        line += m_consoleLogProxy ? "on" : "off";
    }
    return line;
}

YomkResponse YomkLogger::loggers(YomkPkgPtr pkg)
{
    (void)pkg;
    std::vector<std::string> lines;
    {
        // 按锁序 console -> file 嵌套双读锁：两段为同一时刻的原子快照
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        // 预分配避免扩容，缩短双锁窗口
        lines.reserve(m_consoleLoggers.size() + m_fileLoggers.size());
        for (auto &iter : m_consoleLoggers)
            lines.push_back(iter.first + " [console]");
        for (auto &iter : m_fileLoggers)
            lines.push_back(iter.first + " [file] dir:" + iter.second->getDir());
    }
    // 两表为 std::map，中序遍历天然字典序，无需排序（选型见 YomkLogger.h）
    // move 入包消除全量拷贝
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, std::move(lines))};
}

YomkResponse YomkLogger::loggerInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yName);
    {
        std::shared_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        if (m_consoleLoggers.find(yName->d) != m_consoleLoggers.end())
            return {YomkResponse::eOk, yName->d + " [console]"};
    }
    {
        std::shared_lock<std::shared_mutex> lock(m_fileLoggersMutex);
        auto itLogger = m_fileLoggers.find(yName->d);
        if (itLogger != m_fileLoggers.end())
            return {YomkResponse::eOk, yName->d + " [file] dir:" + itLogger->second->getDir()};
    }
    YOMK_ERR_POS_LOG("logger: " + yName->d + " not found.");
    return YomkResponse(YomkResponse::eNo, "logger not found.");
}

YomkResponse YomkLogger::listAll(YomkPkgPtr pkg)
{
    (void)pkg;
    std::vector<std::string> lines;
    // 状态行在双锁外取：四个级别开关为独立 atomic，组合本无原子快照保证
    lines.push_back(consoleLevelLine());
    {
        // 同 loggers：按锁序嵌套双读锁取跨段原子快照
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        // 同 loggers：预分配缩短双锁窗口（+1 为首行状态行）
        lines.reserve(m_consoleLoggers.size() + m_fileLoggers.size() + 1);
        for (auto &iter : m_consoleLoggers)
            lines.push_back(iter.first + " [console]");
        for (auto &iter : m_fileLoggers)
            lines.push_back(iter.first + " [file] dir:" + iter.second->getDir());
    }
    // 同 loggers：std::map 天然字典序，move 入包消除拷贝
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, std::move(lines))};
}
