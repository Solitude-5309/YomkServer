#include "YomkServerPrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServerPrivate::addService(YomkService *srv)
{
    if(!srv)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check the service." << std::endl;
        return;
    }
    std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
    m_serviceMap[srv->name()].reset(srv);
}

YomkResponse YomkServerPrivate::request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg)
{
    std::shared_ptr<YomkService> srv;
    {
        std::shared_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service not found. " << srvName << ", please start the service." << std::endl;
            return YomkResponse(YomkResponse::eErr, "service not found: " + srvName);
        }
        srv = iter->second;
    }
    return srv->invoke(funcName, pkg);
}
