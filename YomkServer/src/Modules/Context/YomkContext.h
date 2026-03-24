# pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>

class YomkContext : public YomkService
{
public:
    YomkContext(YomkServer* server);
    ~YomkContext() {}
public:
    virtual int init() override;
private:
    YomkResponse create(YomkPkgPtr pkg);
    YomkResponse destroy(YomkPkgPtr pkg);
    YomkResponse get(YomkPkgPtr pkg);
    YomkResponse set(YomkPkgPtr pkg);
    YomkResponse turnOnChecker(YomkPkgPtr pkg);
    YomkResponse turnOffChecker(YomkPkgPtr pkg);
    YomkResponse turnOnMonitor(YomkPkgPtr pkg);
    YomkResponse turnOffMonitor(YomkPkgPtr pkg);
    YomkResponse setChecker(YomkPkgPtr pkg);
    YomkResponse setMonitor(YomkPkgPtr pkg);
private:
    std::atomic<bool> m_checkerEnabled;
    std::atomic<bool> m_monitorEnabled;
    std::map<std::string, YomkPkgPtr> m_contexts;
    std::shared_mutex m_contextsMutex;
    std::map<std::string, YContextSetChecker::YomkContextCheckFunc> m_checkers;
    std::shared_mutex m_checkersMutex;
    std::map<std::string, std::vector<YomkContextMonitorFunc>> m_contextMonitors;
    std::shared_mutex m_contextMonitorsMutex;
};