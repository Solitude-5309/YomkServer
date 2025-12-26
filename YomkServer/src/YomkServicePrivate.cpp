#include "YomkServicePrivate.h"
#include "YomkServer.h"
#include <iostream>

void YomkServicePrivate::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    m_funcMap[funcName] = func;
}

YomkRespond YomkServicePrivate::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    YomkServiceFunc tmpFunc = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
        auto iter = m_funcMap.find(funcName);
        if (iter == m_funcMap.end())
        {
            std::cout << " [YomkServicePrivate::invoke] function not found -> " << funcName << std::endl;
            return { YomkRespond::eErr, " [YomkServicePrivate::invoke] function not found. " };
        }
        tmpFunc = iter->second;
    }
    return tmpFunc(pkg);
}

YomkRespond YomkServicePrivate::request(const std::string &url, YomkPkgPtr pkg)
{
    if(m_server)
    {
        return m_server->request(url, pkg);
    }

    return { YomkRespond::eErr, " [YomkServicePrivate::request] server is null. " };
}

void YomkServicePrivate::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkRespondFunc func)
{
    if(m_server)
    {
        m_server->asyncRequest(url, pkg, func);
    }
}
