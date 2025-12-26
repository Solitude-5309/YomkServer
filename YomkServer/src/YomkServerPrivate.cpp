#include "YomkServerPrivate.h"
#include "YomkServer.h"
#include <iostream>

void YomkServerPrivate::addService(YomkService *srv)
{
    if(srv)
    {
        std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
        m_serviceMap[srv->name()].reset(srv);
    }
}

YomkRespond YomkServerPrivate::request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg)
{
    std::shared_ptr<YomkService> srv;
    {
        std::shared_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            std::cout << " [YomkServerPrivate::request] service not found. " << srvName << std::endl;
            return YomkRespond(YomkRespond::eErr, " [YomkServerPrivate::request] service not found. ");
        }
        srv = iter->second;
    }
    return srv->invoke(funcName, pkg);
}
