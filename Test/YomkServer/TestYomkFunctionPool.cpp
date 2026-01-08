#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

YomkResponse func1(YomkServer* server, YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkResponse(YomkResponse::eInvalid, "YString is null");
    }

    std::cout << "func1 called with data: " << yString->d << std::endl;

    if(server)
    {
        YomkResponse response = server->request("/YomkSettings/load", yString);
        if(response.m_resStatus == YomkResponse::eOk)
        {
            std::cout << "load success" << std::endl;
        }
        else
        {
            std::cout << "load failed: " << response.m_msg << std::endl;
        }

        // get cfg_bool
        response = server->request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool"));
        if(response.m_resStatus == YomkResponse::eOk)
        {
            std::cout << "get cfg_bool success" << std::endl;
            YomkUnPackPkg(response.m_data, "YSettingBool", YSettingBool, yBool);
            std::cout << "cfg_bool: " << yBool->d << std::endl;
        }
        else
        {
            std::cout << "get cfg_bool failed: " << response.m_msg << std::endl;
        }
    }

    return {YomkResponse::eOk, "func1 success. "};
}

int main(int argc, char *argv[])
{
    std::string settingsPath = argv[0] + std::string("/../../Settings/settings.json");

    YomkServer server;
    server.startService({ "/YomkSettings", "/YomkFunctionPool" });
    
    YomkResponse response = server.request("/YomkFunctionPool/register", YomkMkYFunctionPtr("func1", func1));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "register func1 success" << std::endl;
    }
    else
    {
        std::cout << "register func1 failed: " << response.m_msg << std::endl;
    }

    response = server.request("/YomkFunctionPool/call", YomkMkYCallFunctionPtr("func1", YomkMkYStringPtr(settingsPath)));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "call func1 success" << std::endl;
    }
    else
    {
        std::cout << "call func1 failed: " << response.m_msg << std::endl;
    }
    
    std::cout << "test YomkFunctionPool completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
