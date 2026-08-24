#include "YomkService.h"
#include "YomkServicePrivate.h"
#include "YomkServer.h"
#include <iostream>

YomkService::YomkService(YomkServer *server)
    : m_p(nullptr)
{
    if (server)
    {
        std::weak_ptr<YomkServer> weakServer = server->weak_from_this();
        if (weakServer.expired())
        {
            YOMK_ERR_POS_LOG("server is not managed by shared_ptr, weak bind failed");
        }
        m_p.reset(new YomkServicePrivate(weakServer));
    }
    else
    {
        YOMK_ERR_POS_LOG("server is null");
    }
}

void YomkService::name(const std::string &name)
{
    if (name.empty())
    {
        YOMK_ERR_POS_LOG("name is empty, set name failed, use default name");
        return;
    }

    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return;
    }
    m_p->name(name);
}

std::string YomkService::name()
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return "";
    }
    return m_p->name();
}

void YomkService::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return;
    }
    m_p->installFunc(funcName, func);
}

YomkResponse YomkService::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return YomkResponse(YomkResponse::eNo, "service is null");
    }
    return m_p->invoke(funcName, pkg);
}

YomkResponse YomkService::request(const std::string &url, YomkPkgPtr pkg)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return YomkResponse(YomkResponse::eNo, "service is null");
    }
    return m_p->request(url, pkg);
}

void YomkService::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("service is null, please check service");
        return;
    }
    m_p->asyncRequest(url, pkg, func);
}
