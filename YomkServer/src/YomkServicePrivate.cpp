#include "YomkServicePrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServicePrivate::installFunc(const std::string &funcName, YomkServiceFunc func)
{
    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    if (m_funcMap.find(funcName) != m_funcMap.end())
    {
        YOMK_ERR_POS_LOG("install function already exists -> " + funcName + ", update to current function");
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
            YOMK_ERR_POS_LOG("function not found -> " + funcName + ", please use YomkInstallFunc to install this function.");
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
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return { YomkResponse::eErr, "server is null" };
    }

    return m_server->request(url, pkg);
}

void YomkServicePrivate::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if(!m_server)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return;
    }
    m_server->asyncRequest(url, pkg, func);
}
