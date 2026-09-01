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
    // asyncThreadCount：异步请求池线程数，0 时由 YomkSimpleThreadPool 取默认值；
    // 异步监控池归 Context 模块自持，服务器只持有请求池
    explicit YomkServerPrivate(std::size_t asyncThreadCount = 0)
        : m_requestPool(asyncThreadCount) {}
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
    // 框架内部投递口：异步请求池不对用户暴露。
    // 服务器已关闭或池已停止时返回 false，由调用方记日志丢弃
    bool postRequestTask(std::function<void()> task);
    YomkResponse request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg = nullptr);
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);

private:
    std::map<std::string, std::shared_ptr<YomkService>> m_serviceMap;
    std::shared_mutex m_serviceMapMtx;
    std::atomic<bool> m_shutdown{false};
    // 异步请求池：异步监控池归 Context 模块自持，随服务 deinit 停止
    YomkSimpleThreadPool m_requestPool;
};
