#include <iostream>
#include "YomkAPI.h"

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
    std::shared_ptr<YomkServer> server = std::make_shared<YomkServer>();
    server->startService({ 
        "/YomkSettings", 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    YOMK_INIT(server);

    YomkResponse response;

    response = YOMK_CONTEXT_CREATE("ctx", YomkMkYStringPtr("ctx_data"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "create context [ ctx = ctx_data ] success" << std::endl;
    }
    else
    {
        std::cout << "create context [ ctx = ctx_data ] failed" << std::endl;
    }

    YStringPtr ctx_data = YOMK_CONTEXT_GET(YString, "ctx", YomkMkYStringPtr("ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    response = YOMK_CONTEXT_SET("ctx", YomkMkYStringPtr("ctx_data_set"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set ] failed" << std::endl;
    }

    ctx_data = YOMK_CONTEXT_GET(YString, "ctx", YomkMkYStringPtr("ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    response = YOMK_CONTEXT_ON_CHECHER();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on checker success" << std::endl;
    }
    else
    {
        std::cout << "turn on checker failed" << std::endl;
    }

    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerAcceptFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set accept checker success" << std::endl;
    }
    else
    {
        std::cout << "set accept checker failed" << std::endl;
    }

    response = YOMK_CONTEXT_ON_MONITOR();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on monitor success" << std::endl;
    }
    else
    {
        std::cout << "turn on monitor failed" << std::endl;
    }
        
    response = YOMK_CONTEXT_SET_MONITOR("ctx", monitorFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set monitor success" << std::endl;
    }
    else
    {
        std::cout << "set monitor failed" << std::endl;
    }

    response = YOMK_CONTEXT_SET("ctx", YomkMkYStringPtr("ctx_data_set_2"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set_2 ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set_2 ] failed" << std::endl;
    }

    ctx_data = YOMK_CONTEXT_GET(YString, "ctx", YomkMkYStringPtr("ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerRejectFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set reject checker success" << std::endl;
    }
    else
    {
        std::cout << "set reject checker failed" << std::endl;
    }

    response = YOMK_CONTEXT_SET("ctx", YomkMkYStringPtr("ctx_data_set_3"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set_3 ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set_3 ] failed" << std::endl;
    }

    ctx_data = YOMK_CONTEXT_GET(YString, "ctx", YomkMkYStringPtr("ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    response = YOMK_CONTEXT_DESTROY("ctx");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "destroy context [ ctx ] success" << std::endl;
    }
    else
    {
        std::cout << "destroy context [ ctx ] failed" << std::endl;
    }

    ctx_data = YOMK_CONTEXT_GET(YString, "ctx", YomkMkYStringPtr("ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    std::cout << "test YomkContext completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
