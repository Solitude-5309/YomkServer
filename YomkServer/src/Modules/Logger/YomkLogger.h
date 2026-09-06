#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include <map>
#include <shared_mutex>
#include <mutex>
#include <atomic>
using namespace yomk;
class YomkLogger : public YomkService
{
public:
    YomkLogger(YomkServer *server);
    virtual ~YomkLogger();

public:
    virtual int init();

private:
    YomkResponse consoleLog(YomkPkgPtr pkg);
    YomkResponse setConsoleLogProxy(YomkPkgPtr pkg);
    YomkResponse createFileLogger(YomkPkgPtr pkg);
    YomkResponse fileLog(YomkPkgPtr pkg);
    YomkResponse writeFileLog(YomkPkgPtr pkg);
    YomkResponse deleteLogger(YomkPkgPtr pkg);
    YomkResponse offConsoleLogByLevel(YomkPkgPtr pkg);
    YomkResponse onConsoleLogByLevel(YomkPkgPtr pkg);

private:
    YomkResponse loggers(YomkPkgPtr pkg);
    YomkResponse loggerInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);
    std::string consoleLevelLine();

private:
    // 锁序约定（LG3/P3-a）：需要同时读取两张 logger 表时（loggers/listAll 内省快照），
    // 一律按 m_consoleLoggersMutex -> m_fileLoggersMutex 的顺序嵌套持有，全仓无反向嵌套点
    // （consoleLog 的 proxy 锁在独立作用域释放后才取 console 锁；loggerInfo/fileLog/
    // writeFileLog/createFileLogger 均单锁）；m_consoleLogProxyMutex 为叶子锁，
    // 其临界区内不得获取任何其他锁。后续维护新增嵌套时须沿用此序，否则有死锁风险。
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::shared_mutex m_consoleLoggersMutex;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
    std::shared_mutex m_fileLoggersMutex;
    std::atomic<bool> m_showConsoleDebugLog;
    std::atomic<bool> m_showConsoleInfoLog;
    std::atomic<bool> m_showConsoleWarningLog;
    std::atomic<bool> m_showConsoleErrorLog;
    // proxy 字段专属锁（LG2/P1-b）：setConsoleLogProxy 写与 consoleLog 读均持锁，
    // 回调在锁外执行（锁内拷贝、锁外调用，避免持锁重入）
    std::mutex m_consoleLogProxyMutex;
    bool m_consoleLogProxy;
    YomkConsoleLogProxyFunc m_consoleLogProxyFunc;
};