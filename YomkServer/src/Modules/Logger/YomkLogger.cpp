#include "YomkLogger.h"
#include <iostream>

// LG5（静态分析清零）：4 个级别开关与 2 个 proxy 字段原在构造函数体内赋值，触发
// cppcoreguidelines-pro-type-member-init（std::atomic 默认构造不初始化值）与
// cppcoreguidelines-prefer-member-initializer ×2 → 全部移入成员初始化列表。
// 列表顺序严格按 YomkLogger.h 的声明序（四个 atomic 在前、proxy 两个在后，中间的
// m_consoleLogProxyMutex 默认构造即可、无需显式列出），否则触发 -Wreorder 违反零告警门禁。
// 语义等价：构造期对象尚未暴露给任何线程，atomic 的直接构造与 .store(true) 同为
// seq_cst 且值相同；顺序变化仅体现为 atomic/proxy 现在先于函数体的 map 赋值完成
// （原为反序），而 ConsoleLogger 构造不读这些字段 → 无副作用。
YomkLogger::YomkLogger(YomkServer *server)
    : YomkService(server), m_showConsoleDebugLog(true) // 声明序 YomkLogger.h:50-53，四个 atomic 不得相互调换
      ,
      m_showConsoleInfoLog(true), m_showConsoleWarningLog(true), m_showConsoleErrorLog(true), m_consoleLogProxy(false) // 声明序 YomkLogger.h:57-58
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

    // P1-b（LG2 修复）：proxy 字段在专属锁内拷贝快照，回调在锁外调用，
    // 与 setConsoleLogProxy 的写互斥，且避免用户回调持锁重入
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

    // P1-a（LG2 修复）：双检锁——shared_lock 只读查找，miss 后升级 unique_lock
    // 并二次查找（其他线程可能已创建），写 map 仅在独占锁下进行；
    // 拷贝 logger 指针后离开锁作用域，级别 switch 在锁外执行
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
            auto result = m_consoleLoggers.emplace(log->d.m_logger, newLogger);
            if (!result.second)
            {
                YOMK_ERR_POS_LOG("console logger: " + log->d.m_logger + " create failed.");
                return YomkResponse(YomkResponse::eNo, "console logger: " + log->d.m_logger + " create failed.");
            }
            itLogger = result.first;
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
    // P1-b（LG2 修复）：与 consoleLog 的读互斥
    std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
    m_consoleLogProxy = true;
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    return {YomkResponse::eOk, "success."};
}

YomkResponse YomkLogger::createFileLogger(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, LogFile, logFile);

    // P2-a（LG2 修复）：空名/空目录校验，拒绝 "" key 注册与 "/.log" 相对路径回退
    // （对齐 FunctionPool registerFunction 空名 eInvalid 惯例）
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
    // P2-b（LG2 修复）：init 失败（目录不可建/文件不可开）不再异常穿透，
    // 且不注册幽灵 logger
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

    // 空名校验与 createFileLogger 同惯例（P2-a）：拒绝 "" key
    if (yName->d.empty())
    {
        YOMK_ERR_POS_LOG("logger name is empty.");
        return YomkResponse(YomkResponse::eInvalid, "logger name is empty.");
    }

    // 分段独占、不嵌套：删除不需要跨表原子快照（"只存在于其中一张表"本就是合法状态），
    // 而同时持两把写锁会把全部日志写入挡在整个删除过程之外。此处不嵌套持有，
    // 故与 P3-a 的锁序约定无冲突（该约定只约束"同时持有两把锁"的内省快照场景）
    size_t deletedConsole = 0;
    {
        std::unique_lock<std::shared_mutex> lock(m_consoleLoggersMutex);
        deletedConsole = m_consoleLoggers.erase(yName->d);
    }

    // 锁内拷出指针、锁外析构（沿用 P1-b 范式）：~FileLogger 会调 write() 落盘，
    // 若让最后一个引用在写锁内释放，该文件 I/O 将阻塞所有 file 日志写入
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

    // 不删除磁盘上的 .log 文件（数据保全，清理归调用方）；"MainLogger" 无特殊保护，
    // 删除后下次控制台日志按 consoleLog 的双检锁惰性重建
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
    // P1-b（LG2 修复）：内省读侧同样需与 setConsoleLogProxy 的写互斥
    // （叶子锁：调用方 listAll 在本函数外不持任何锁，无嵌套无死锁风险）
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
    // P3-a（LG3 修复）：按锁序 console -> file 嵌套持有两把读锁，使 console 段与 file 段
    // 成为同一时刻的原子快照。原分段持锁时 file 段的读取时刻晚于 console 段，并发创建
    // logger 会返回从未真实存在过的行数组合（LG3 S5 判别式实测：1800 对成对创建下
    // LOGGERS 违例 8 个、ALL 违例 7 个）
    std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
    std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
    // P4-c（LG4 修复）：取到两把读锁后立即预分配，避免万级条目下 vector<string> 反复扩容
    // 搬移，缩短双锁快照窗口——该窗口会阻塞 createFileLogger 的独占锁。
    // 不改变 P3-a 的嵌套双锁原子性语义（LG3 S5 判别式仍须为 0 违例）
    lines.reserve(m_consoleLoggers.size() + m_fileLoggers.size());
    for (auto &iter : m_consoleLoggers)
        lines.push_back(iter.first + " [console]");
    for (auto &iter : m_fileLoggers)
        lines.push_back(iter.first + " [file] dir:" + iter.second->getDir());
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
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
    // 状态行在双锁之外取：m_consoleLogProxyMutex 是叶子锁，且四个级别开关为独立 atomic，
    // 其组合本就无原子快照保证（既有设计，不在 P3-a 范围）
    lines.push_back(consoleLevelLine());
    // P3-a（LG3 修复）：同 loggers，行数快照按锁序 console -> file 嵌套取，跨段原子
    std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
    std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
    // P4-c（LG4 修复）：同 loggers，预分配缩短双锁快照窗口（+1 为首行状态行）
    lines.reserve(m_consoleLoggers.size() + m_fileLoggers.size() + 1);
    for (auto &iter : m_consoleLoggers)
        lines.push_back(iter.first + " [console]");
    for (auto &iter : m_fileLoggers)
        lines.push_back(iter.first + " [file] dir:" + iter.second->getDir());
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}
