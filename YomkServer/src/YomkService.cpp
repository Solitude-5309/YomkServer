#include "YomkService.h"
#include "YomkServicePrivate.h"
#include <iostream>

YomkService::YomkService(YomkServer *server)
    : m_p(nullptr)
{
    if(!server)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null" << std::endl;
    }
    m_p.reset(new YomkServicePrivate(server));
}

void YomkService::name(const std::string &name)
{
    if(name.empty()) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "name is empty, set name failed, please check name" << std::endl;
        return;
    }

    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return;
    }
    m_p->name(name);
}

std::string YomkService::name()
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return "";
    }
    return m_p->name();
}

void YomkService::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return;
    }
    m_p->installFunc(funcName, func);
}

YomkResponse YomkService::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return YomkResponse(YomkResponse::eErr, "service is null");
    }
    return m_p->invoke(funcName, pkg);
}

YomkResponse YomkService::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return YomkResponse(YomkResponse::eErr, "service is null");
    }
    return m_p->request(url, pkg);
}

void YomkService::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "service is null, please check service" << std::endl;
        return;
    }
    m_p->asyncRequest(url, pkg, func);
}
