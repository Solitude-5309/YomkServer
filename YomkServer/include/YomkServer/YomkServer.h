#pragma once
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <vector>

#include "YomkService.h"

class YomkServerPrivate;
class YOMKSERVER_EXPORT YomkServer : public std::enable_shared_from_this<YomkServer>
{
public:
    YomkServer();
    virtual ~YomkServer() {}

public:
    template <typename T>
    int newService(const std::string &srvName = "")
    {
        YomkService *srv = new T(this);

        if (srvName != "")
            srv->name(srvName);

        addService(srv);
        return 0;
    }

public:
    int startService(std::vector<std::string> srvNames);
    void addService(YomkService *srv);
    int delService(const std::string &srvName);
    // 关闭后不支持重新注册/启动服务（框架单例依赖 std::call_once 初始化）
    void shutdown();
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    std::shared_ptr<YomkServerPrivate> m_p;
};
