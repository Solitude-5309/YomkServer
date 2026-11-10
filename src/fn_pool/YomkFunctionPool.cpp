#include "YomkFunctionPool.h"

#include <iostream>
#include <vector>
#include "yomk/YomkDiag.h"
#include "yomk/fn_pool/YomkFunctionPoolMsg.h"
#include "yomk/srv_info/YomkServerInfoMsg.h"
YomkFunctionPool::YomkFunctionPool(YomkServer* server) : YomkService(server)
{
    name("/YomkFunctionPool");
}

int YomkFunctionPool::init()
{
    installFunc("/register", &YomkFunctionPool::registerFunction, "Function");
    installFunc("/unregister", &YomkFunctionPool::unRegisterFunction, "String");
    installFunc("/call", &YomkFunctionPool::callFunction, "CallFunction");
    // 调试内省接口,端点挂在本服务 funcMap
    installFunc("/names", &YomkFunctionPool::funcNames);
    installFunc("/name", &YomkFunctionPool::funcInfo, "String");
    installFunc("/all", &YomkFunctionPool::listAll);
    return 0;
}

yomk::payload::Response YomkFunctionPool::registerFunction(YomkMsgPtr pkg)
{
    auto yFunc = yomk::msgs::unpack<yomk::msgs::Function>(pkg);
    if (!yFunc)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not Function, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    if (yFunc->d.m_funcName.empty() || yFunc->d.m_func == nullptr)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "funcName or func is empty");
    }

    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);

    auto itFunc = m_functions.find(yFunc->d.m_funcName);
    if (itFunc != m_functions.end())
    {
        itFunc->second.m_func = yFunc->d.m_func;
        itFunc->second.m_msgName = yFunc->d.m_msgName;
        itFunc->second.m_description = yFunc->d.m_description;
        YOMK_DIAG_INFO("function '" + yFunc->d.m_funcName + "' already exists, hot-swap to current function");
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "function '" + yFunc->d.m_funcName + "' already exists, hot-swap to current function");
    }
    m_functions.emplace(yFunc->d.m_funcName, PoolFuncInfo{yFunc->d.m_func, yFunc->d.m_msgName, yFunc->d.m_description});
    return {yomk::payload::ResStatus::Succeeded, "register function success"};
}

yomk::payload::Response YomkFunctionPool::unRegisterFunction(YomkMsgPtr pkg)
{
    auto yFuncName = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!yFuncName)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    if (yFuncName->d.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "funcName is empty");
    }

    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);
    auto itFunc = m_functions.find(yFuncName->d);
    if (itFunc == m_functions.end())
    {
        // 名字有效但未注册:ResStatus::Failed(与 funcInfo/Context/EventLoop 的 not-found 惯例一致;ResStatus::Invalid 仅用于参数无效)
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "function '" + yFuncName->d + "' is not registered");
    }
    m_functions.erase(itFunc);
    return {yomk::payload::ResStatus::Succeeded, "unregister function success"};
}

yomk::payload::Response YomkFunctionPool::callFunction(YomkMsgPtr pkg)
{
    auto yCallFunc = yomk::msgs::unpack<yomk::msgs::CallFunction>(pkg);
    if (!yCallFunc)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not CallFunction, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    if (yCallFunc->d.m_funcName.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "funcName is empty");
    }

    YomkServiceFunc copyFunc;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        auto itFunc = m_functions.find(yCallFunc->d.m_funcName);
        if (itFunc == m_functions.end())
        {
            // 未注册同 unregister:ResStatus::Failed(not-found 框架惯例;Invalid 仅用于参数无效)
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "function '" + yCallFunc->d.m_funcName + "' is not registered");
        }
        copyFunc = itFunc->second.m_func;
    }

    return copyFunc(yCallFunc->d.m_pkg);
}

// 内省:列出全部注册函数名
yomk::payload::Response YomkFunctionPool::funcNames(YomkMsgPtr pkg)
{
    std::vector<std::string> funcNames;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        for (auto& iter : m_functions)
        {
            funcNames.push_back(iter.first);
        }
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(funcNames)};
}

// 内省:单个函数元信息查询,命中返回结构化 FuncInfo 包
yomk::payload::Response YomkFunctionPool::funcInfo(YomkMsgPtr pkg)
{
    auto yFuncName = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!yFuncName)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    if (yFuncName->d.empty())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "funcName is empty");
    }

    std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
    auto itFunc = m_functions.find(yFuncName->d);
    if (itFunc == m_functions.end())
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "function '" + yFuncName->d + "' is not registered");
    }
    // 命中:结构化包恒挂
    yomk::payload::FuncInfo info;
    info.m_funcName = yFuncName->d;
    info.m_msgName = itFunc->second.m_msgName;
    info.m_description = itFunc->second.m_description;
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::FuncInfo::shared(info)};
}

// 内省:全量函数元信息,锁内构造结构化快照(总数由 d.size() 承载)
yomk::payload::Response YomkFunctionPool::listAll(YomkMsgPtr pkg)
{
    std::vector<yomk::payload::FuncInfo> infos;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        infos.reserve(m_functions.size());
        for (auto& iter : m_functions)
        {
            yomk::payload::FuncInfo info;
            info.m_funcName = iter.first;
            info.m_msgName = iter.second.m_msgName;
            info.m_description = iter.second.m_description;
            infos.push_back(std::move(info));
        }
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::FuncInfoList::shared(infos)};
}
