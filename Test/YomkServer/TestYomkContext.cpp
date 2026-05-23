#include <iostream>
#include "YomkAPI.h"

// CTX设置前进入check函数，用于CTX的创建者屏蔽非法操作，check函数接受才能真正的修改CTX
ContextChecker::ECheckStatus checkerAcceptFunc(YomkPkgPtr pkg)
{
    std::cout << "checker accept func called. " << std::endl;
    return ContextChecker::eAccept;
}

// CTX设置前进入check函数，用于CTX的创建者屏蔽非法操作，check函数拒绝则不能修改CTX
ContextChecker::ECheckStatus checkerRejectFunc(YomkPkgPtr pkg)
{
    std::cout << "checker reject func called. " << std::endl;
    return ContextChecker::eReject;
}

// CTX设置成功后进入monitor函数，用于CTX的监控者接收CTX的变化
void monitorFunc(YomkPtr(Context) ctx)
{
    YomkUnPackPkgVoid(ctx->d.m_value, string, yStr);
    std::cout << "monitor func called. ctx: " << yStr->d << std::endl;
}

int main(int argc, char *argv[])
{
    YOMK_INIT(std::make_shared<YomkServer>(), { 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });

    YomkResponse response;

    // 创建CTX
    response = YOMK_CONTEXT_CREATE("ctx", YomkMkPtr(string, "ctx_data"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "create context [ ctx = ctx_data ] success" << std::endl;
    }
    else
    {
        std::cout << "create context [ ctx = ctx_data ] failed" << std::endl;
    }

    // 获取CTX
    YomkPtr(string) ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    // 设置CTX
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set ] failed" << std::endl;
    }

    // 获取CTX
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    // 开启CTX检查，开启check后，才能在CTX设置前调用检查函数
    response = YOMK_CONTEXT_ON_CHECKER();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on checker success" << std::endl;
    }
    else
    {
        std::cout << "turn on checker failed" << std::endl;
    }

    // 设置CTX检查函数
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerAcceptFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set accept checker success" << std::endl;
    }
    else
    {
        std::cout << "set accept checker failed" << std::endl;
    }

    // 开启CTX监控，开启monitor后，才能在CTX设置成功后调用监控函数
    response = YOMK_CONTEXT_ON_MONITOR();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "turn on monitor success" << std::endl;
    }
    else
    {
        std::cout << "turn on monitor failed" << std::endl;
    }
        
    // 设置CTX监控函数，用于CTX的监控者接收CTX的变化
    response = YOMK_CONTEXT_SET_MONITOR("ctx", monitorFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set monitor success" << std::endl;
    }
    else
    {
        std::cout << "set monitor failed" << std::endl;
    }

    // 再次设置CTX，此时将会先进入检查函数，再真正设置CTX，最后进入CTX监控函数
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set_2"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set_2 ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set_2 ] failed" << std::endl;
    }

    // 获取CTX，更新后的CTX
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    // 设置CTX检查函数为拒绝，此时设置CTX将被拒绝
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerRejectFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set reject checker success" << std::endl;
    }
    else
    {
        std::cout << "set reject checker failed" << std::endl;
    }

    // 再次设置CTX，此时将会被拒绝，无法设置成功，也无法进入监控函数
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set_3"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set context [ ctx = ctx_data_set_3 ] success" << std::endl;
    }
    else
    {
        std::cout << "set context [ ctx = ctx_data_set_3 ] failed" << std::endl;
    }

    // 获取CTX，更新后的CTX，无法更新
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    // 销毁CTX
    response = YOMK_CONTEXT_DESTROY("ctx");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "destroy context [ ctx ] success" << std::endl;
    }
    else
    {
        std::cout << "destroy context [ ctx ] failed" << std::endl;
    }

    // 再次获取CTX，此时CTX已销毁，将返回默认值
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    std::cout << "get ctx: " << ctx_data->d << std::endl;

    std::cout << "test YomkContext completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
