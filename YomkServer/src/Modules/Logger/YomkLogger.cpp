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
            // P3-b（LG6 修复）：原此处有 if (!result.second) 防御分支（emplace 失败则记错并
            // 返回 eNo），该分支不可达——本块持 m_consoleLoggersMutex 的独占锁，且上方 find 已
            // 确认 key 为 miss，容器 emplace 在「独占 + key 确认不存在」下必然插入成功
            // （result.second 恒 true）。LG4/LG5 的 gcov 实测印证：那两行是 YomkLogger.cpp 中
            // 仅有的 ##### 零计数可执行行（LG6/V9 复核陈旧 .gcov 逐位确认，##### 恰为这两行）。
            // 删除后取 emplace 免费返回的迭代器，省掉 auto result 与 if 判断两行。
            // 控制流拓扑刻意与修复前保持一致：不新增 else 分支，仍由块外统一 itLogger->second
            // 赋值。LG6/V9 实测否决了「两条路径各自赋值」的写法——那样会新增一个 else 分支，
            // 而该分支只在双检锁竞争窗口（其他线程在 shared_lock 释放与 unique_lock 获取之间
            // 建好同名 logger）才执行，28 个二进制累积 .gcda 后从未撞中，等于用一个 ##### 洞
            // 换掉原来的两个；块外统一赋值则恒有计数，且两条路径共用一行、语义与修复前逐字相同。
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
    // P1-b（LG2 修复）：与 consoleLog 的读互斥
    std::lock_guard<std::mutex> proxyLock(m_consoleLogProxyMutex);
    m_consoleLogProxyFunc = consoleLogProxy->d.m_consoleLogProxyFunc;
    // P4-e（LG6 修复）：开关不再恒置 true，而由回调是否为空决定——传空回调即卸载 proxy、
    // 恢复框架默认的控制台输出。原实现安装后进程内不可卸载：YOMK_SET_CONSOLE_LOG_PROXY(nullptr)
    // 仍置 proxy:on，而 m_consoleLogProxyFunc 为空使 consoleLog 的三重条件短路，行为等同未安装
    // 却在内省里谎报 proxy:on。两字段现由同一把叶子锁内一致更新（该字段的三个访问点为
    // setConsoleLogProxy 写、consoleLog 读并回调、consoleLevelLine 内省汇总，后两者早已持锁），
    // 卸载后 consoleLog 的 proxyEnabled 为 false，直接走默认输出路径。
    m_consoleLogProxy = (m_consoleLogProxyFunc != nullptr);
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
    {
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
    }
    // P4-d（LG6 实测否决）：两表保持 std::map，中序遍历天然字典序，故此处无需排序即满足内省
    // 输出的既有可读契约（console 段字典序在前、file 段字典序在后）。曾按 LG4 登记意图改为
    // std::unordered_map + 锁外分段排序，四方消融实测使本端点由 3.05 ms 退化至 12.48 ms
    // （N=20000，四轮交错中位）、S4C 单查 -10.6%，而六条热路径与 VmHWM 零收益，已回退；
    // 完整数据与理由详见 YomkLogger.h 的容器选型注释。
    // P4-g（LG6 修复）：move 入包，消除 vector<string> 的一次全量拷贝（YomkMsg 宏已补右值
    // 构造重载，详见 YomkPkg.h）。21 万条目下该拷贝是单次内省数十 MB 瞬时分配的主因之一
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
    // 状态行在双锁之外取：m_consoleLogProxyMutex 是叶子锁，且四个级别开关为独立 atomic，
    // 其组合本就无原子快照保证（既有设计，不在 P3-a 范围）
    lines.push_back(consoleLevelLine());
    {
        // P3-a（LG3 修复）：同 loggers，行数快照按锁序 console -> file 嵌套取，跨段原子
        std::shared_lock<std::shared_mutex> consoleLock(m_consoleLoggersMutex);
        std::shared_lock<std::shared_mutex> fileLock(m_fileLoggersMutex);
        // P4-c（LG4 修复）：同 loggers，预分配缩短双锁快照窗口（+1 为首行状态行）
        lines.reserve(m_consoleLoggers.size() + m_fileLoggers.size() + 1);
        for (auto &iter : m_consoleLoggers)
            lines.push_back(iter.first + " [console]");
        for (auto &iter : m_fileLoggers)
            lines.push_back(iter.first + " [file] dir:" + iter.second->getDir());
    }
    // P4-d（LG6 实测否决）：同 loggers，两表保持 std::map 故无需排序；首行状态行不是日志器行，
    // 天然不参与两段字典序。回退理由与数据详见 loggers() 与 YomkLogger.h。
    // P4-g（LG6 修复）：同 loggers，move 入包消除一次全量拷贝
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, std::move(lines))};
}
