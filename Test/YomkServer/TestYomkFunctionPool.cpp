#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

YomkRespond func1(YomkServer* server, YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkRespond(YomkRespond::eInvalid, "YString is null");
    }

    std::cout << "func1 called with data: " << yString->d << std::endl;

    if(server)
    {
        YomkRespond respond = server->request("/YomkSettings/load", yString);
        if(respond.m_resStatus == YomkRespond::eOk)
        {
            std::cout << "load success" << std::endl;
        }
        else
        {
            std::cout << "load failed: " << respond.m_msg << std::endl;
        }

        // get cfg_bool
        respond = server->request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool"));
        if(respond.m_resStatus == YomkRespond::eOk)
        {
            std::cout << "get cfg_bool success" << std::endl;
            YomkUnPackPkg(respond.m_data, "YSettingBool", YSettingBool, yBool);
            std::cout << "cfg_bool: " << yBool->d << std::endl;
        }
        else
        {
            std::cout << "get cfg_bool failed: " << respond.m_msg << std::endl;
        }
    }

    return {YomkRespond::eOk, "func1 success. "};
}

int main(int argc, char *argv[])
{
    std::string settingsPath = argv[0] + std::string("/../../Settings/settings.json");

    YomkServer server;
    server.startService({ "/YomkSettings", "/YomkFunctionPool" });
    
    YomkRespond respond = server.request("/YomkFunctionPool/register", YomkMkYFunctionPtr("func1", func1));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "register func1 success" << std::endl;
    }
    else
    {
        std::cout << "register func1 failed: " << respond.m_msg << std::endl;
    }

    respond = server.request("/YomkFunctionPool/call", YomkMkYCallFunctionPtr("func1", YomkMkYStringPtr(settingsPath)));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "call func1 success" << std::endl;
    }
    else
    {
        std::cout << "call func1 failed: " << respond.m_msg << std::endl;
    }
    
    std::cout << "test YomkFunctionPool completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
