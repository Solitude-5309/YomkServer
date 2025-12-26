#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

YContextSetChecker::ECheckStatus checkerAcceptFunc(YomkPkgPtr pkg)
{
    std::cout << "checker accept func called. " << std::endl;
    return YContextSetChecker::eAccept;
}

YContextSetChecker::ECheckStatus checkerRejectFunc(YomkPkgPtr pkg)
{
    std::cout << "checker reject func called. " << std::endl;
    return YContextSetChecker::eReject;
}

void monitorFunc(YContextPtr ctx)
{
    YomkUnPackPkgVoid(ctx->m_value, "YString", YString, yStr);
    std::cout << "monitor func called. ctx: " << yStr->d << std::endl;
}

int main(int argc, char *argv[])
{
    YomkServer server;
    server.startService({ 
        "/YomkSettings", 
        "/YomkFunctionPool", 
        "/YomkContext" });
    
    YomkRespond respond = server.request("/YomkContext/create", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "create context success" << std::endl;
    }
    else
    {
        std::cout << "create context failed" << std::endl;
    }

    respond = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    respond = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    respond = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    respond = server.request("/YomkContext/turn_on_checker");
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "turn on checker success" << std::endl;
    }
    else
    {
        std::cout << "turn on checker failed" << std::endl;
    }

    respond = server.request("/YomkContext/set_checker", YomkMkYContextSetCheckerPtr("ctx", checkerAcceptFunc));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set checker success" << std::endl;
    }
    else
    {
        std::cout << "set checker failed" << std::endl;
    }

    respond = server.request("/YomkContext/turn_on_monitor");
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "turn on monitor success" << std::endl;
    }
    else
    {
        std::cout << "turn on monitor failed" << std::endl;
    }
        
    respond = server.request("/YomkContext/set_monitor", YomkMkYContextMonitorPtr("ctx", monitorFunc));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set monitor success" << std::endl;
    }
    else
    {
        std::cout << "set monitor failed" << std::endl;
    }

    respond = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set_2")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    respond = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    respond = server.request("/YomkContext/set_checker", YomkMkYContextSetCheckerPtr("ctx", checkerRejectFunc));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set checker success" << std::endl;
    }
    else
    {
        std::cout << "set checker failed" << std::endl;
    }

    respond = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set_3")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    respond = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    respond = server.request("/YomkContext/destroy", YomkMkYStringPtr("ctx"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "destroy context success" << std::endl;
    }
    else
    {
        std::cout << "destroy context failed" << std::endl;
    }

    respond = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
        YomkUnPackPkg(respond.m_data, "YString", YString, yStr);
        if(yStr != nullptr)
        {
            std::cout << "ctx default: " << yStr->d << std::endl;
        }
    }

    std::cout << "test YomkContext completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
