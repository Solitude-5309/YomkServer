#include "YomkServicePrivate.h"

#include <iostream>
#include <mutex>

#include "YomkServer.h"

void YomkServicePrivate::name(const std::string& name)
{
    if (m_registered)
    {
        YOMK_ERR_POS_LOG("service is registered, rename is not allowed -> " + name);
        return;
    }
    m_name = name;
}

void YomkServicePrivate::installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName,
                                     const std::string& description)
{
    if (funcName.empty() || funcName[0] != '/')
    {
        YOMK_ERR_POS_LOG("function name parse error: " + funcName + ", please start with /");
        return;
    }

    // 空函数对象拒绝安装：否则 invoke 命中后执行空 std::function 抛 bad_function_call 终止进程
    if (!func)
    {
        YOMK_ERR_POS_LOG("function is null, install function refused -> " + funcName);
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    if (m_funcMap.find(funcName) != m_funcMap.end())
    {
        YOMK_ERR_POS_LOG("install function already exists -> " + funcName + ", update to current function");
    }

    m_funcMap[funcName] = func;
    if (!msgName.empty() || !description.empty())
    {
        yomk::FuncInfo info;
        info.m_funcName = funcName;
        info.m_msgName = msgName;
        info.m_description = description;
        m_funcInfoMap[funcName] = info;
    }
    else
    {
        // 两参覆盖安装时清除残留的函数信息，保持内省与注册一致
        m_funcInfoMap.erase(funcName);
    }
}

std::map<std::string, yomk::FuncInfo> YomkServicePrivate::funcInfos()
{
    std::map<std::string, yomk::FuncInfo> infos;
    std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
    for (auto& iter : m_funcMap)
    {
        yomk::FuncInfo info;
        info.m_funcName = iter.first;
        auto itInfo = m_funcInfoMap.find(iter.first);
        if (itInfo != m_funcInfoMap.end())
        {
            info.m_msgName = itInfo->second.m_msgName;
            info.m_description = itInfo->second.m_description;
        }
        infos.emplace(iter.first, info);
    }
    return infos;
}

YomkResponse YomkServicePrivate::invoke(const std::string& funcName, YomkPkgPtr pkg)
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
            YOMK_ERR_POS_LOG(
                "function not found -> " + funcName + ", please use installFunc to install this function.");
            return {YomkResponse::eNo, "function not found: " + funcName};
        }
        tmpFunc = iter->second;
    }

    return tmpFunc(pkg);
}

YomkResponse YomkServicePrivate::request(const std::string& url, YomkPkgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, request ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    return server->request(url, pkg);
}

void YomkServicePrivate::asyncRequest(const std::string& url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, async request ignored.");
        return;
    }
    server->asyncRequest(url, pkg, func);
}
