#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "YomkService.h"

class YomkServerPrivate;
class YOMKSERVER_EXPORT YomkServer : public std::enable_shared_from_this<YomkServer>
{
public:
    // 创建 YomkServer 实例（由 shared_ptr 管理），asyncThreadCount 为异步请求线程池大小
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0)
    {
        return std::shared_ptr<YomkServer>(new YomkServer(asyncThreadCount));
    }
    virtual ~YomkServer();

public:
    template <typename T>
    int newService(const std::string& srvName = "")
    {
        YomkService* srv = new T(this);

        if (srvName != "")
            srv->name(srvName);

        return addService(srv);
    }

public:
    int startService(std::vector<std::string> srvNames);
    int addService(YomkService* srv);
    int delService(const std::string& srvName);
    void shutdown();
    std::vector<std::string> serviceNames();
    std::map<std::string, yomk::FuncInfo> serviceFuncInfos(const std::string& srvName);
    yomk::payload::Response request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
