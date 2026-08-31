#pragma once
#include <atomic>
#include <string>
#include <map>
#include <memory>
#include <shared_mutex>
#include <vector>
#include "YomkPkg.h"

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
    // 调用后不支持二次初始化（init 依赖 std::call_once），假定在主线程调用
    void shutdown();
    YomkResponse request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg = nullptr);
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);

private:
    std::map<std::string, std::shared_ptr<YomkService>> m_serviceMap;
    std::shared_mutex m_serviceMapMtx;
    std::atomic<bool> m_shutdown{false};
};
