#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
using namespace yomk;
class YomkContext : public YomkService
{
public:
    struct ContextMonitor
    {
        bool asyncMonitor;
        YomkContextMonitorFunc contextMonitorFunc;
    };
    struct Context
    {
        std::string key;
        YomkPkgPtr value;
        YomkContextCheckFunc checker;
        std::vector<ContextMonitor> monitors;
    };

public:
    YomkContext(YomkServer *server);
    virtual ~YomkContext() {}

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
    std::map<std::string, Context> m_contexts;
    std::shared_mutex m_contextsMutex;
};