# pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include <map>
#include <set>
class YomkContext : public YomkService
{
public:
    YomkContext(YomkServer* server);
    ~YomkContext() {}
public:
    virtual int init() override;
private:
    YomkRespond create(YomkPkgPtr pkg);
    YomkRespond destroy(YomkPkgPtr pkg);
    YomkRespond get(YomkPkgPtr pkg);
    YomkRespond set(YomkPkgPtr pkg);
    YomkRespond turnOnChecker(YomkPkgPtr pkg);
    YomkRespond turnOffChecker(YomkPkgPtr pkg);
    YomkRespond turnOnMonitor(YomkPkgPtr pkg);
    YomkRespond turnOffMonitor(YomkPkgPtr pkg);
    YomkRespond setChecker(YomkPkgPtr pkg);
    YomkRespond setMonitor(YomkPkgPtr pkg);
private:
    bool m_checkerEnabled;
    bool m_monitorEnabled;
    std::map<std::string, YomkPkgPtr> m_contexts;
    std::map<std::string, YContextSetChecker::YomkContextCheckFunc> m_checkers;
    std::map<std::string, std::vector<YomkContextMonitorFunc>> m_contextMonitors;
};