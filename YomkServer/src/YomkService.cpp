#include "YomkService.h"
#include "YomkServicePrivate.h"
#include <iostream>

YomkService::YomkService(YomkServer *server)
    : m_p(nullptr)
{
    if(server)
    {
        m_p.reset(new YomkServicePrivate(server));
    }
}

void YomkService::name(const std::string &name)
{
    if(name.empty()) return;
    if(!m_p) return;
    m_p->name(name);
}

std::string YomkService::name()
{
    if(!m_p) return "";
    return m_p->name();
}

void YomkService::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    if(!m_p) return;
    m_p->installFunc(funcName, func);
}

YomkRespond YomkService::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    if(!m_p) return YomkRespond();
    return m_p->invoke(funcName, pkg);
}

YomkRespond YomkService::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_p) return YomkRespond();
    return m_p->request(url, pkg);
}

void YomkService::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkRespondFunc func)
{
    if(!m_p) return ;
    m_p->asyncRequest(url, pkg, func);
}
