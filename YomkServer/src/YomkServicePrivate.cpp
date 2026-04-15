#include "YomkServicePrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServicePrivate::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    if (m_funcMap.find(funcName) != m_funcMap.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "install function already exists -> " << funcName << ", update to current function" << std::endl;
    }
    m_funcMap[funcName] = func;
}

YomkResponse YomkServicePrivate::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    YomkServiceFunc tmpFunc = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
        auto iter = m_funcMap.find(funcName);
        if (iter == m_funcMap.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "function not found -> " << funcName << ", please use YomkInstallFunc to install this function." << std::endl;
            return { YomkResponse::eErr, "function not found: " + funcName };
        }
        tmpFunc = iter->second;
    }
    return tmpFunc(pkg);
}

YomkResponse YomkServicePrivate::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_server)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null, please start the server." << std::endl;
        return { YomkResponse::eErr, "server is null" };
    }

    return m_server->request(url, pkg);
}

void YomkServicePrivate::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if(!m_server)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null, please start the server." << std::endl;
        return;
    }
    m_server->asyncRequest(url, pkg, func);
}
