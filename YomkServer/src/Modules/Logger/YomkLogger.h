#pragma once
#include "YomkServer.h"
#include "YomkDefine.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include <map>

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
private:
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
};