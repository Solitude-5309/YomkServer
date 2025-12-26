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

YomkRespond YomkFunctionPool::registerFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YFunction", YFunction, yFunc);
    
    if(yFunc->m_funcName.empty() || yFunc->m_func == nullptr)
    {
        return YomkRespond(YomkRespond::eInvalid, "funcName or func is empty");
    }

    if(m_functions.find(yFunc->m_funcName) != m_functions.end())
    {
        m_functions[yFunc->m_funcName] = yFunc->m_func;
        return YomkRespond(YomkRespond::eOk, "find function name is exist, and update it");
    }
    else
    {
        m_functions[yFunc->m_funcName] = yFunc->m_func;
    }

    return {YomkRespond::eOk, "register function success"};
}

YomkRespond YomkFunctionPool::callFunction(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YCallFunction", YCallFunction, yCallFunc);
    
    if(yCallFunc->m_funcName.empty())
    {
        return YomkRespond(YomkRespond::eInvalid, "funcName is empty");
    }

    if(m_functions.find(yCallFunc->m_funcName) == m_functions.end())
    {
        return YomkRespond(YomkRespond::eInvalid, "funcName is not register");
    }

    return m_functions[yCallFunc->m_funcName](m_server, yCallFunc->m_pkg);
}

