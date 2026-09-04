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
        bool asyncMonitor = false;
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
    // 异步监控池：Context 模块自持，固定单线程 FIFO；post 在 set 写锁内完成 ⇒ 入队序=提交序 ⇒ 异步通知恒按 set 提交序送达（并发 set 亦然），drain-on-stop 不丢；
    // 锁序不变量 m_contextsMutex -> pool.m_mtx 单向：worker 弹出任务即释放池锁再锁外执行、deinit 不持 contexts 锁调 stop、stop 不持池锁 join，无反向同时持有故无 ABBA；
    // init 重建（池 stop 后不可复用，重建支持服务删除后重新注册），deinit 排空停止
    std::unique_ptr<YomkSimpleThreadPool> m_monitorPool;
};