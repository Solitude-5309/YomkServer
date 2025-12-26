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
    YomkRespond createConsoleLogger(YomkPkgPtr pkg);
    YomkRespond consoleLog(YomkPkgPtr pkg);
    YomkRespond createFileLogger(YomkPkgPtr pkg);
    YomkRespond fileLog(YomkPkgPtr pkg);
    YomkRespond writeFileLog(YomkPkgPtr pkg);
private:
    std::map<std::string, ConsoleLoggerPtr> m_consoleLoggers;
    std::map<std::string, FileLoggerPtr> m_fileLoggers;
};