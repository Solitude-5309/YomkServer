#include "YomkServicePrivate.h"

#include <iostream>
#include <mutex>

#include "yomk/server/YomkServer.h"

void YomkServicePrivate::name(const std::string& name)
{
    if (m_registered)
    {
        YOMK_DIAG_WARN("service rename refused, already registered, refused name '" + name + "'");
        return;
    }
    m_name = name;
}

void YomkServicePrivate::installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName,
                                     const std::string& description)
{
    if (funcName.empty() || funcName[0] != '/')
    {
        YOMK_DIAG_WARN("function name parse error: " + funcName + ", please start with /");
        return;
    }

    // 空函数对象拒绝安装:否则 invoke 命中后执行空 std::function 抛 bad_function_call 终止进程
    if (!func)
    {
        YOMK_DIAG_WARN("function is null, install function refused: " + funcName);
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_funcMapMtx);
    if (m_funcMap.find(funcName) != m_funcMap.end())
    {
        // C-5 已采纳:重复安装即覆盖既有注册,级别 WARN(默认最小级别下 INFO 会被抑制,热替换将无感知)
        YOMK_DIAG_WARN("function '" + funcName + "' already exists, replace with current function");
    }

    m_funcMap[funcName] = func;
    if (!msgName.empty() || !description.empty())
    {
        yomk::payload::FuncInfo info;
        info.m_funcName = funcName;
        info.m_msgName = msgName;
        info.m_description = description;
        m_funcInfoMap[funcName] = info;
    }
    else
    {
        // 两参覆盖安装时清除残留的函数信息,保持内省与注册一致
        m_funcInfoMap.erase(funcName);
    }
}

std::map<std::string, yomk::payload::FuncInfo> YomkServicePrivate::funcInfos()
{
    std::map<std::string, yomk::payload::FuncInfo> infos;
    std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
    for (auto& iter : m_funcMap)
    {
        yomk::payload::FuncInfo info;
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

yomk::payload::Response YomkServicePrivate::invoke(std::string_view funcName, YomkMsgPtr pkg)
{
    if (funcName.empty() || funcName[0] != '/')
    {
        return {yomk::payload::ResStatus::Failed, "function name parse error: " + std::string(funcName) + ", please start with /"};
    }

    YomkServiceFunc tmpFunc = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_funcMapMtx);
        auto iter = m_funcMap.find(funcName);
        if (iter == m_funcMap.end())
        {
            return {yomk::payload::ResStatus::Failed, "function not found: " + std::string(funcName)};
        }
        tmpFunc = iter->second;
    }

    return tmpFunc(pkg);
}

yomk::payload::Response YomkServicePrivate::request(std::string_view url, YomkMsgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, request ignored");
        return {yomk::payload::ResStatus::Failed, "server has been destroyed"};
    }

    return server->request(url, pkg);
}

void YomkServicePrivate::asyncRequest(std::string_view url, YomkMsgPtr pkg, YomkResponseFunc func)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, async request ignored");
        return;
    }
    server->asyncRequest(url, pkg, func);
}
