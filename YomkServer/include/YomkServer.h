#pragma once
#include <string>
#include <memory>
#include <map>
#include <functional>

#include "YomkService.h"

class YomkServerPrivate;
class YOMKSERVER_EXPORT YomkServer
{
public:
    YomkServer();
    ~YomkServer() {}
public:
    template<typename T>
    int newService(const std::string& srvName = "")
    {
        YomkService* srv = new T(this);
        srv->name(srvName);

        if (srv->init() != 0)
        {
            return 1;
        }

        addService(srv);
        return 0;
    }
public:
    int startService(std::vector<std::string> srvNames);
    void addService(YomkService* srv);
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
private:
    std::shared_ptr<YomkServerPrivate> m_p;
};
