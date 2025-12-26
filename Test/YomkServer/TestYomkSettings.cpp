#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

int main(int argc, char *argv[])
{
    std::string settingsPath = argv[0] + std::string("/../../Settings/settings.json");

    YomkServer server;
    server.startService({ "/YomkSettings" });

    // load settings
    YomkRespond respond = server.request("/YomkSettings/load", YomkMkYStringPtr(settingsPath));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "load success" << std::endl;
    }
    else
    {
        std::cout << "load failed: " << respond.m_msg << std::endl;
    }

    // get cfg_bool
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool"));
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

    // get cfg_bool_array
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool_array"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_bool_array success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingBoolArray", YSettingBoolArray, yBoolArray);
        for(int i = 0; i < yBoolArray->d.size(); i++)
        {
            std::cout << "cfg_bool_array[" << i << "] = " << yBoolArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_bool_array failed: " << respond.m_msg << std::endl;
    }

    // get cfg_double
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_double"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_double success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingDouble", YSettingDouble, yDouble);
        std::cout << "cfg_double: " << yDouble->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_double failed: " << respond.m_msg << std::endl;
    }

    // get cfg_double_array
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_double_array"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_double_array success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingDoubleArray", YSettingDoubleArray, yDoubleArray);
        for(int i = 0; i < yDoubleArray->d.size(); i++)
        {
            std::cout << "cfg_double_array[" << i << "] = " << yDoubleArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_double_array failed: " << respond.m_msg << std::endl;
    }

    // get cfg_int
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_int"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_int success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingInt", YSettingInt, yInt);
        std::cout << "cfg_int: " << yInt->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_int failed: " << respond.m_msg << std::endl;
    }

    // get cfg_int_array
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_int_array"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_int_array success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingIntArray", YSettingIntArray, yIntArray);
        for(int i = 0; i < yIntArray->d.size(); i++)
        {
            std::cout << "cfg_int_array[" << i << "] = " << yIntArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_int_array failed: " << respond.m_msg << std::endl;
    }

    // get cfg_string
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_string"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_string success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingString", YSettingString, yStr);
        std::cout << "cfg_string: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_string failed: " << respond.m_msg << std::endl;
    }

    // get cfg_string_array
    respond = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_string_array"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "get cfg_string_array success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YSettingStringArray", YSettingStringArray, yStrArray);
        for(int i = 0; i < yStrArray->d.size(); i++)
        {
            std::cout << "cfg_string_array[" << i << "] = " << yStrArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_string_array failed: " << respond.m_msg << std::endl;
    }

    // set cfg_bool
    respond = server.request("/YomkSettings/set", YomkMkYSettingBoolPtr("cfg_bool", true));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_bool success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool failed: " << respond.m_msg << std::endl;
    }

    // set cfg_bool_array
    respond = server.request("/YomkSettings/set", YomkMkYSettingBoolArrayPtr("cfg_bool_array", { false, false, false, true, true, true }));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_bool_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool_array failed: " << respond.m_msg << std::endl;
    }

    // set cfg_double
    respond = server.request("/YomkSettings/set", YomkMkYSettingDoublePtr("cfg_double", 3.1415926));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_double success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double failed: " << respond.m_msg << std::endl;
    }

    // set cfg_double_array
    respond = server.request("/YomkSettings/set", YomkMkYSettingDoubleArrayPtr("cfg_double_array", { 100.1, 200.1, 300.1, 400.1, 500.1, 600.1 }));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_double_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double_array failed: " << respond.m_msg << std::endl;
    }

    // set cfg_int  
    respond = server.request("/YomkSettings/set", YomkMkYSettingIntPtr("cfg_int", 10086));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_int success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int failed: " << respond.m_msg << std::endl;
    }

    // set cfg_int_array
    respond = server.request("/YomkSettings/set", YomkMkYSettingIntArrayPtr("cfg_int_array", { 1000, 2000, 3000, 4000, 5000, 6000 }));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_int_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int_array failed: " << respond.m_msg << std::endl;
    }

    // set cfg_string
    respond = server.request("/YomkSettings/set", YomkMkYSettingStringPtr("cfg_string", "1.0.3"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_string success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string failed: " << respond.m_msg << std::endl;
    }

    // set cfg_string_array
    respond = server.request("/YomkSettings/set", YomkMkYSettingStringArrayPtr("cfg_string_array", { "aa", "bb", "cc", "dd", "ee", "ff" }));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "set cfg_string_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string_array failed: " << respond.m_msg << std::endl;
    }

    // save settings
    respond = server.request("/YomkSettings/save", YomkMkYStringPtr(settingsPath));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "save success" << std::endl;
    }
    else
    {
        std::cout << "save failed: " << respond.m_msg << std::endl;
    }
    
    std::cout << "test settings completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
