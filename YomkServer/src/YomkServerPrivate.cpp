#include "YomkServerPrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServerPrivate::addService(YomkService *srv)
{
    if (!srv)
    {
        YOMK_ERR_POS_LOG("service is null, please check the service.");
        return;
    }
    std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
    if (m_serviceMap.find(srv->name()) != m_serviceMap.end())
    {
        YOMK_ERR_POS_LOG("service already exists -> " + srv->name() + ", update to current service");
    }
    m_serviceMap[srv->name()].reset(srv);
}

int YomkServerPrivate::delService(const std::string &srvName)
{
    std::shared_ptr<YomkService> srv;
    {
        std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            YOMK_ERR_POS_LOG("service not found, can not delete -> " + srvName);
            return -1;
        }
        srv = std::move(iter->second);
        m_serviceMap.erase(iter);
    }

    // 锁外执行 deinit 与析构：在途请求持有的 shared_ptr 副本保证不会与 invoke 并发析构
    srv->deinit();
    return 0;
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
            return YomkResponse(YomkResponse::eNo, "service not found: " + srvName);
        }
        srv = iter->second;
    }
    return srv->invoke(funcName, pkg);
}
