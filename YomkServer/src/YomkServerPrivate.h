#pragma once
#include <atomic>
#include <string>
#include <map>
#include <memory>
#include <shared_mutex>
#include <vector>
#include <functional>
#include "YomkPkg.h"
#include "YomkSimpleThreadPool.h"

class YomkService;
class YomkServerPrivate
{
public:
    YomkServerPrivate() {}
    ~YomkServerPrivate()
    {
        if (!m_shutdown)
            shutdown();
    }

public:
    void addService(YomkService *srv);
    int delService(const std::string &srvName);
    // 关闭服务器：先停异步任务池（拒新 -> 排空 -> join），再清空服务表并在锁外逐个调用 deinit()；
    // 幂等，重复调用直接返回；调用后不支持二次初始化（init 依赖 std::call_once），假定在主线程调用
    void shutdown();
    // 投递异步任务到内部线程池：服务器已关闭或池已停止时返回 false，由调用方记日志丢弃
    bool postAsyncTask(std::function<void()> task);
    YomkResponse request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg = nullptr);
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);

private:
    std::map<std::string, std::shared_ptr<YomkService>> m_serviceMap;
    std::shared_mutex m_serviceMapMtx;
    std::atomic<bool> m_shutdown{false};
    YomkSimpleThreadPool m_asyncPool;
};
