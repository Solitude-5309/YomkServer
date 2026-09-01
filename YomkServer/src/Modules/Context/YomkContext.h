#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include "../../YomkSimpleThreadPool.h"
#include <map>
#include <memory>
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
    // 停止监控池（拒新 -> 排空 -> join）：保证关闭/删除返回前存量异步监控任务已全部执行；
    // stop 幂等，重复 deinit 安全
    virtual void deinit() override;

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
    YomkResponse keys(YomkPkgPtr pkg);
    YomkResponse keyInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);

private:
    std::atomic<bool> m_checkerEnabled;
    std::atomic<bool> m_monitorEnabled;
    std::map<std::string, Context> m_contexts;
    std::shared_mutex m_contextsMutex;
    // 异步监控池：Context 模块自持，固定单线程保证监控事件按 set 顺序到达；
    // init 重建（池 stop 后不可复用，重建支持服务删除后重新注册），deinit 排空停止
    std::unique_ptr<YomkSimpleThreadPool> m_monitorPool;
};