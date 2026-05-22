#include "YomkFunctionPool.h"
#include <iostream>
YomkFunctionPool::YomkFunctionPool(YomkServer *server)
    : YomkService(server)
    , m_server(server)
{
    name("YomkFunctionPool");
}

int YomkFunctionPool::init()
{
    YomkInstallFunc("/register", YomkFunctionPool::registerFunction);
    YomkInstallFunc("/call", YomkFunctionPool::callFunction);
    return 0;
}

YomkResponse YomkFunctionPool::registerFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Function, yFunc);
    if(!yFunc)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "Function is empty, please check Function" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "Function is empty");
    }
    if(yFunc->d.m_funcName.empty() || yFunc->d.m_func == nullptr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName or func is empty, please check Function.m_funcName" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "funcName or func is empty");
    }
    
    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);

    auto itFunc = m_functions.find(yFunc->d.m_funcName);
    if(itFunc != m_functions.end())
    {
        itFunc->second = yFunc->d.m_func;
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "find function name: " << yFunc->d.m_funcName << " is exist, and update it" << std::endl;
        return YomkResponse(YomkResponse::eOk, "find function name is exist, and update it");
    }
    else
    {
        m_functions[yFunc->d.m_funcName] = yFunc->d.m_func;
        return {YomkResponse::eOk, "register function success"};
    }
}

YomkResponse YomkFunctionPool::callFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, CallFunction, yCallFunc);
    if(!yCallFunc)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "CallFunction is empty, please check CallFunction" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "CallFunction is empty");
    }
    if(yCallFunc->d.m_funcName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName is empty, please check CallFunction.m_funcName" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    YomkServiceFunc copyFunc;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        auto itFunc = m_functions.find(yCallFunc->d.m_funcName);
        if(itFunc == m_functions.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName: " << yCallFunc->d.m_funcName << " is not register, please check CallFunction.m_funcName" << std::endl;
            return YomkResponse(YomkResponse::eInvalid, "funcName is not register");
        }
        copyFunc = itFunc->second;
    }

    return copyFunc(yCallFunc->d.m_pkg);
}

