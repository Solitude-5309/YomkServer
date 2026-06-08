#include "YomkServerPrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServerPrivate::addService(YomkService *srv)
{
    if(!srv)
    {
        YOMK_ERR_POS_LOG("service is null, please check the service.");
        return;
    }
    std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
    if (m_serviceMap.find(srv->name()) != m_serviceMap.end())
    {
        YOMK_ERR_POS_LOG("install function already exists -> " + srv->name() + ", update to current function");
    }
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
            YOMK_ERR_POS_LOG("service not found. " + srvName + ", please start the service.");
            return YomkResponse(YomkResponse::eErr, "service not found: " + srvName);
        }
        srv = iter->second;
    }
    return srv->invoke(funcName, pkg);
}
