#include "YomkFunctionPool.h"
#include <iostream>
#include <vector>
YomkFunctionPool::YomkFunctionPool(YomkServer *server)
    : YomkService(server)
{
    name("/YomkFunctionPool");
}

int YomkFunctionPool::init()
{
    YomkInstallFunc("/register", YomkFunctionPool::registerFunction, Function);
    YomkInstallFunc("/unregister", YomkFunctionPool::unRegisterFunction, String);
    YomkInstallFunc("/call", YomkFunctionPool::callFunction, CallFunction);
    // 调试内省接口，端点挂在本服务 funcMap
    YomkInstallFunc("/names", YomkFunctionPool::funcNames);
    YomkInstallFunc("/name", YomkFunctionPool::funcInfo, String);
    YomkInstallFunc("/all", YomkFunctionPool::listAll);
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
        YOMK_ERR_POS_LOG("function name: " + yFunc->d.m_funcName + " is already exist, update to current function");
        return YomkResponse(YomkResponse::eOk, "find function name is already exist, update to current function");
    }
    m_functions.emplace(yFunc->d.m_funcName, PoolFuncInfo{yFunc->d.m_func, yFunc->d.m_msgName});
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
        return YomkResponse(YomkResponse::eInvalid, "funcName is not register");
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
            YOMK_ERR_POS_LOG("funcName: " + yCallFunc->d.m_funcName + " is not register, please check CallFunction.m_funcName");
            return YomkResponse(YomkResponse::eInvalid, "funcName is not register");
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
        for (auto &iter : m_functions)
        {
            funcNames.push_back(iter.first);
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, funcNames)};
}

// 内省：单个函数存在性查询，命中 msg 为 funcName [类型名]（未声明类型时无括号后缀）
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
    const std::string &msgName = itFunc->second.m_msgName;
    return {YomkResponse::eOk, yFuncName->d + (msgName.empty() ? "" : " [" + msgName + "]")};
}

// 内省：全量 dump，首行 functions:N，其余每行 funcName [类型名]（未声明类型时无括号后缀）
YomkResponse YomkFunctionPool::listAll(YomkPkgPtr pkg)
{
    std::vector<std::string> lines;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        lines.push_back("functions:" + std::to_string(m_functions.size()));
        for (auto &iter : m_functions)
        {
            const std::string &msgName = iter.second.m_msgName;
            lines.push_back(iter.first + (msgName.empty() ? "" : " [" + msgName + "]"));
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}
