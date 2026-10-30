#include "YomkFunctionPool.h"

#include <iostream>
#include <vector>
YomkFunctionPool::YomkFunctionPool(YomkServer* server) : YomkService(server)
{
    name("/YomkFunctionPool");
}

int YomkFunctionPool::init()
{
    installFunc("/register", &YomkFunctionPool::registerFunction, "Function");
    installFunc("/unregister", &YomkFunctionPool::unRegisterFunction, "String");
    installFunc("/call", &YomkFunctionPool::callFunction, "CallFunction");
    // 调试内省接口，端点挂在本服务 funcMap
    installFunc("/names", &YomkFunctionPool::funcNames);
    installFunc("/name", &YomkFunctionPool::funcInfo, "String");
    installFunc("/all", &YomkFunctionPool::listAll);
    return 0;
}

YomkResponse YomkFunctionPool::registerFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Function, yFunc);
    if (yFunc->d.m_funcName.empty() || yFunc->d.m_func == nullptr)
    {
        YOMK_ERR_POS_LOG("funcName or func is empty, please check Function.m_funcName");
        return YomkResponse(YomkResponse::eInvalid, "funcName or func is empty");
    }

    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);

    auto itFunc = m_functions.find(yFunc->d.m_funcName);
    if (itFunc != m_functions.end())
    {
        itFunc->second.m_func = yFunc->d.m_func;
        itFunc->second.m_msgName = yFunc->d.m_msgName;
        itFunc->second.m_description = yFunc->d.m_description;
        YOMK_ERR_POS_LOG("function name: " + yFunc->d.m_funcName + " is already exist, update to current function");
        return YomkResponse(YomkResponse::eOk, "find function name is already exist, update to current function");
    }
    m_functions.emplace(yFunc->d.m_funcName, PoolFuncInfo{yFunc->d.m_func, yFunc->d.m_msgName, yFunc->d.m_description});
    return {YomkResponse::eOk, "register function success"};
}

YomkResponse YomkFunctionPool::unRegisterFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yFuncName);
    if (yFuncName->d.empty())
    {
        YOMK_ERR_POS_LOG("funcName is empty, please check String");
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);
    auto itFunc = m_functions.find(yFuncName->d);
    if (itFunc == m_functions.end())
    {
        YOMK_ERR_POS_LOG("funcName: " + yFuncName->d + " is not register, can not unregister");
        // 名字有效但未注册：eNo（与 funcInfo/Context/EventLoop 的 not-found 惯例一致；eInvalid 仅用于参数无效）
        return YomkResponse(YomkResponse::eNo, "funcName is not register");
    }
    m_functions.erase(itFunc);
    return {YomkResponse::eOk, "unregister function success"};
}

YomkResponse YomkFunctionPool::callFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, CallFunction, yCallFunc);
    if (yCallFunc->d.m_funcName.empty())
    {
        YOMK_ERR_POS_LOG("funcName is empty, please check CallFunction.m_funcName");
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    YomkServiceFunc copyFunc;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        auto itFunc = m_functions.find(yCallFunc->d.m_funcName);
        if (itFunc == m_functions.end())
        {
            YOMK_ERR_POS_LOG(
                "funcName: " + yCallFunc->d.m_funcName + " is not register, please check CallFunction.m_funcName");
            // 名字有效但未注册：eNo（not-found 框架惯例；eInvalid 仅用于参数无效）
            return YomkResponse(YomkResponse::eNo, "funcName is not register");
        }
        copyFunc = itFunc->second.m_func;
    }

    return copyFunc(yCallFunc->d.m_pkg);
}

// 内省：列出全部注册函数名
YomkResponse YomkFunctionPool::funcNames(YomkPkgPtr pkg)
{
    std::vector<std::string> funcNames;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        for (auto& iter : m_functions)
        {
            funcNames.push_back(iter.first);
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, funcNames)};
}

// 内省：单个函数元信息查询，命中返回结构化 FuncInfo 包（m_msg 恒 "ok"，数据走 m_data）
YomkResponse YomkFunctionPool::funcInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yFuncName);
    if (yFuncName->d.empty())
    {
        YOMK_ERR_POS_LOG("funcName is empty, please check String");
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
    auto itFunc = m_functions.find(yFuncName->d);
    if (itFunc == m_functions.end())
    {
        YOMK_ERR_POS_LOG("funcName: " + yFuncName->d + " is not register, please check funcName");
        return YomkResponse(YomkResponse::eNo, "funcName is not register");
    }
    // 命中：结构化包恒挂，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）
    yomk::FuncInfo info;
    info.m_funcName = yFuncName->d;
    info.m_msgName = itFunc->second.m_msgName;
    info.m_description = itFunc->second.m_description;
    return {YomkResponse::eOk, "ok", YomkMkPtr(FuncInfo, info)};
}

// 内省：全量函数元信息，锁内构造结构化快照（m_msg 恒 "ok"，数据走 m_data；总数由 d.size() 承载）
YomkResponse YomkFunctionPool::listAll(YomkPkgPtr pkg)
{
    std::vector<yomk::FuncInfo> infos;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        infos.reserve(m_functions.size());
        for (auto& iter : m_functions)
        {
            yomk::FuncInfo info;
            info.m_funcName = iter.first;
            info.m_msgName = iter.second.m_msgName;
            info.m_description = iter.second.m_description;
            infos.push_back(std::move(info));
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(FuncInfoList, infos)};
}
