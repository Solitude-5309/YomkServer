#include <iostream>
#include "YomkAPI.h"

// CTX设置前进入check函数，用于CTX的创建者屏蔽非法操作，check函数接受才能真正的修改CTX
ContextChecker::ECheckStatus checkerAcceptFunc(const yomk::Context& ctx)
{
    YomkUnPackPkg(ctx.m_value, string, str);
    if(!str)
    {
        YOMK_ERROR_TAG("checkerAcceptFunc", "checker accept func called. value is null");
        return ContextChecker::eReject;
    }
    YOMK_DEBUG_TAG("checkerAcceptFunc", "checker accept func called. ctx: key = ", ctx.m_key, "value = ", str->d);
    return ContextChecker::eAccept;
}

// CTX设置前进入check函数，用于CTX的创建者屏蔽非法操作，check函数拒绝则不能修改CTX
ContextChecker::ECheckStatus checkerRejectFunc(const yomk::Context& ctx)
{
    YomkUnPackPkg(ctx.m_value, string, str);
    if(!str)
    {
        YOMK_ERROR_TAG("checkerRejectFunc", "checker reject func called. value is null");
        return ContextChecker::eReject;
    }
    YOMK_DEBUG_TAG("checkerRejectFunc", "checker reject func called. ctx: key = ", ctx.m_key, "value = ", str->d);
    
    return ContextChecker::eReject;
}

// CTX设置成功后进入monitor函数，用于CTX的监控者接收CTX的变化
void monitorFunc(const yomk::Context& ctx)
{
    YomkUnPackPkgVoid(ctx.m_value, string, str);
    if(!str)
    {
        YOMK_ERROR_TAG("monitorFunc", "monitor func called. value is null");
        return;
    }
    YOMK_DEBUG_TAG("monitorFunc", "monitor func called. ctx: key= ", ctx.m_key, ", value = ", str->d);
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
        YOMK_DEBUG_TAG("main", "create context [ ctx = ctx_data ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "create context [ ctx = ctx_data ] failed");
    }

    // 获取CTX
    YomkPtr(string) ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d);

    // 设置CTX
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set context [ ctx = ctx_data_set ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set ] failed");
    }

    // 获取CTX
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d);

    // 开启CTX检查，开启check后，才能在CTX设置前调用检查函数
    response = YOMK_CONTEXT_ON_CHECKER();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "turn on checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "turn on checker failed");
    }

    // 设置CTX检查函数
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerAcceptFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set accept checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set accept checker failed");
    }

    // 开启CTX监控，开启monitor后，才能在CTX设置成功后调用监控函数
    response = YOMK_CONTEXT_ON_MONITOR();
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "turn on monitor success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "turn on monitor failed");
    }
        
    // 设置CTX监控函数，用于CTX的监控者接收CTX的变化
    response = YOMK_CONTEXT_SET_MONITOR("ctx", monitorFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set monitor success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set monitor failed");
    }

    // 再次设置CTX，此时将会先进入检查函数，再真正设置CTX，最后进入CTX监控函数
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set_2"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set context [ ctx = ctx_data_set_2 ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_2 ] failed");
    }

    // 获取CTX，更新后的CTX
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d);

    // 设置CTX检查函数为拒绝，此时设置CTX将被拒绝
    response = YOMK_CONTEXT_SET_CHECKER("ctx", checkerRejectFunc);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "set reject checker success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set reject checker failed");
    }

    // 再次设置CTX，此时将会被拒绝，无法设置成功，也无法进入监控函数
    response = YOMK_CONTEXT_SET("ctx", YomkMkPtr(string, "ctx_data_set_3"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_3 ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "set context [ ctx = ctx_data_set_3 ] failed");
    }

    // 获取CTX，更新后的CTX，无法更新
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d);

    // 销毁CTX
    response = YOMK_CONTEXT_DESTROY("ctx");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "destroy context [ ctx ] success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "destroy context [ ctx ] failed");
    }

    // 再次获取CTX，此时CTX已销毁，将返回默认值
    ctx_data = YOMK_CONTEXT_GET(Yomk(string), "ctx", YomkMkPtr(string, "ctx_data_default"));
    YOMK_DEBUG_TAG("main", "get ctx: ", ctx_data->d);

    YOMK_DEBUG_TAG("main", "test YomkContext completed, any key to continue...");

    getchar();

    return 0;
}
