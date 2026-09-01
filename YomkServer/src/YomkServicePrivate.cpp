#include "YomkServicePrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServicePrivate::name(const std::string &name)
{
    if (m_registered)
    {
        YOMK_ERR_POS_LOG("service is registered, rename is not allowed -> " + name);
        return;
    }
    m_name = name;
}

void YomkServicePrivate::installFunc(const std::string &funcName, YomkServiceFunc func, const std::string &msgName)
{
    if (funcName.empty() || funcName[0] != '/')
    {
        YOMK_ERR_POS_LOG("function name parse error: " + funcName + ", please start with /");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    if (m_funcMap.find(funcName) != m_funcMap.end())
    {
        YOMK_ERR_POS_LOG("install function already exists -> " + funcName + ", update to current function");
    }

    m_funcMap[funcName] = func;
    if (!msgName.empty())
    {
        m_funcMsgMap[funcName] = msgName;
    }
    else
    {
        // 两参覆盖安装时清除残留的旧类型元数据，保持内省与注册一致
        m_funcMsgMap.erase(funcName);
    }
}

std::map<std::string, YomkFuncInfo> YomkServicePrivate::funcInfos()
{
    std::map<std::string, YomkFuncInfo> infos;
    std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
    for (auto &iter : m_funcMap)
    {
        YomkFuncInfo info;
        info.m_funcName = iter.first;
        auto itMsg = m_funcMsgMap.find(iter.first);
        if (itMsg != m_funcMsgMap.end())
        {
            info.m_msgName = itMsg->second;
        }
        infos.emplace(iter.first, info);
    }
    return infos;
}

YomkResponse YomkServicePrivate::invoke(const std::string &funcName, YomkPkgPtr pkg)
{
    if (funcName.empty() || funcName[0] != '/')
    {
        YOMK_ERR_POS_LOG("function name parse error: " + funcName + ", please start with /");
        return {YomkResponse::eNo, "function name parse error: " + funcName + ", please start with /"};
    }

    YomkServiceFunc tmpFunc = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
        auto iter = m_funcMap.find(funcName);
        if (iter == m_funcMap.end())
        {
            YOMK_ERR_POS_LOG("function not found -> " + funcName + ", please use YomkInstallFunc to install this function.");
            return {YomkResponse::eNo, "function not found: " + funcName};
        }
        tmpFunc = iter->second;
    }

    return tmpFunc(pkg);
}

YomkResponse YomkServicePrivate::request(const std::string &url, YomkPkgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, request ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    return server->request(url, pkg);
}

void YomkServicePrivate::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, async request ignored.");
        return;
    }
    server->asyncRequest(url, pkg, func);
}

bool YomkServicePrivate::postAsyncTask(std::function<void()> task)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, async task ignored.");
        return false;
    }
    return server->postAsyncTask(std::move(task));
}
