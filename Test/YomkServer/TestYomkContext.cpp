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
    
    YomkResponse response = server.request("/YomkContext/create", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "create context success" << std::endl;
    }
    else
    {
        std::cout << "create context failed" << std::endl;
    }

    response = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    response = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    response = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    response = server.request("/YomkContext/turn_on_checker");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on checker success" << std::endl;
    }
    else
    {
        std::cout << "turn on checker failed" << std::endl;
    }

    response = server.request("/YomkContext/set_checker", YomkMkYContextSetCheckerPtr("ctx", checkerAcceptFunc));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set checker success" << std::endl;
    }
    else
    {
        std::cout << "set checker failed" << std::endl;
    }

    response = server.request("/YomkContext/turn_on_monitor");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on monitor success" << std::endl;
    }
    else
    {
        std::cout << "turn on monitor failed" << std::endl;
    }
        
    response = server.request("/YomkContext/set_monitor", YomkMkYContextMonitorPtr("ctx", monitorFunc));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set monitor success" << std::endl;
    }
    else
    {
        std::cout << "set monitor failed" << std::endl;
    }

    response = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set_2")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    response = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    response = server.request("/YomkContext/set_checker", YomkMkYContextSetCheckerPtr("ctx", checkerRejectFunc));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set checker success" << std::endl;
    }
    else
    {
        std::cout << "set checker failed" << std::endl;
    }

    response = server.request("/YomkContext/set", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_set_3")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context success" << std::endl;
    }
    else
    {
        std::cout << "set context failed" << std::endl;
    }

    response = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
    }

    response = server.request("/YomkContext/destroy", YomkMkYStringPtr("ctx"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "destroy context success" << std::endl;
    }
    else
    {
        std::cout << "destroy context failed" << std::endl;
    }

    response = server.request("/YomkContext/get", YomkMkYContextPtr("ctx", YomkMkYStringPtr("ctx_data_default")));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get context success" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        std::cout << "ctx: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get context failed" << std::endl;
        YomkUnPackPkg(response.m_data, "YString", YString, yStr);
        if(yStr != nullptr)
        {
            std::cout << "ctx default: " << yStr->d << std::endl;
        }
    }

    std::cout << "test YomkContext completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
