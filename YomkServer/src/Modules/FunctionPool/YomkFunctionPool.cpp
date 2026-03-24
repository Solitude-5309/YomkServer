#include "YomkFunctionPool.h"

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
    YomkUnPackPkgresponse(pkg, "YFunction", YFunction, yFunc);
    
    if(yFunc->m_funcName.empty() || yFunc->m_func == nullptr)
    {
        return YomkResponse(YomkResponse::eInvalid, "funcName or func is empty");
    }

    
    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);

    auto itFunc = m_functions.find(yFunc->m_funcName);
    if(itFunc != m_functions.end())
    {
        itFunc->second = yFunc->m_func;
        return YomkResponse(YomkResponse::eOk, "find function name is exist, and update it");
    }
    else
    {
        m_functions[yFunc->m_funcName] = yFunc->m_func;
        return {YomkResponse::eOk, "register function success"};
    }
}

YomkResponse YomkFunctionPool::callFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YCallFunction", YCallFunction, yCallFunc);
    
    if(yCallFunc->m_funcName.empty())
    {
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    YomkServiceFunc copyFunc;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        auto itFunc = m_functions.find(yCallFunc->m_funcName);
        if(itFunc == m_functions.end())
        {
            return YomkResponse(YomkResponse::eInvalid, "funcName is not register");
        }
        copyFunc = itFunc->second;
    }

    return copyFunc(yCallFunc->m_pkg);
}

