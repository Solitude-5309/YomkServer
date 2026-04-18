#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include <map>
#include <shared_mutex>
#include <mutex>
#include <atomic>

class YomkLogger : public YomkService
{
public:
    YomkLogger(YomkServer* server);
    ~YomkLogger();
public:
    virtual int init();
private:
    YomkResponse createConsoleLogger(YomkPkgPtr pkg);
    YomkResponse consoleLog(YomkPkgPtr pkg);
    YomkResponse createFileLogger(YomkPkgPtr pkg);
    YomkResponse fileLog(YomkPkgPtr pkg);
    YomkResponse writeFileLog(YomkPkgPtr pkg);
    YomkResponse offConsoleLogByLevel(YomkPkgPtr pkg);
    YomkResponse onConsoleLogByLevel(YomkPkgPtr pkg);
private:
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::shared_mutex m_consoleLoggersMutex;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
    std::shared_mutex m_fileLoggersMutex;
    std::atomic<bool> m_showConsoleDebugLog;
    std::atomic<bool> m_showConsoleInfoLog;
    std::atomic<bool> m_showConsoleWarningLog;
    std::atomic<bool> m_showConsoleErrorLog;
};