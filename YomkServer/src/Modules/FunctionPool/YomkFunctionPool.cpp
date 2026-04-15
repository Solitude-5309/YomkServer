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
    YomkUnPackPkgresponse(pkg, "YFunction", YFunction, yFunc);
    if(!yFunc)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YFunction is empty, please check YFunction" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "YFunction is empty");
    }
    if(yFunc->m_funcName.empty() || yFunc->m_func == nullptr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName or func is empty, please check YFunction" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "funcName or func is empty");
    }
    
    std::unique_lock<std::shared_mutex> lock(m_functionsMutex);

    auto itFunc = m_functions.find(yFunc->m_funcName);
    if(itFunc != m_functions.end())
    {
        itFunc->second = yFunc->m_func;
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "find function name is exist, and update it" << std::endl;
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
    if(!yCallFunc)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YCallFunction is empty, please check YCallFunction" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "YCallFunction is empty");
    }
    if(yCallFunc->m_funcName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName is empty, please check YCallFunction.m_funcName" << std::endl;
        return YomkResponse(YomkResponse::eInvalid, "funcName is empty");
    }

    YomkServiceFunc copyFunc;
    {
        std::shared_lock<std::shared_mutex> lock(m_functionsMutex);
        auto itFunc = m_functions.find(yCallFunc->m_funcName);
        if(itFunc == m_functions.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "funcName is not register, please check YCallFunction.m_funcName" << std::endl;
            return YomkResponse(YomkResponse::eInvalid, "funcName is not register");
        }
        copyFunc = itFunc->second;
    }

    return copyFunc(yCallFunc->m_pkg);
}

