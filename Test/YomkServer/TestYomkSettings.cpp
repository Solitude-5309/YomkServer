#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

int main(int argc, char *argv[])
{
    std::string settingsPath = argv[0] + std::string("/../../Settings/settings.json");

    YomkServer server;
    server.startService({ "/YomkSettings" });

    // load settings
    YomkResponse response = server.request("/YomkSettings/load", YomkMkYStringPtr(settingsPath));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "load success" << std::endl;
    }
    else
    {
        std::cout << "load failed: " << response.m_msg << std::endl;
    }

    // get cfg_bool
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool"));
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

    // get cfg_bool_array
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_bool_array"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_bool_array success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingBoolArray", YSettingBoolArray, yBoolArray);
        for(int i = 0; i < yBoolArray->d.size(); i++)
        {
            std::cout << "cfg_bool_array[" << i << "] = " << yBoolArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_bool_array failed: " << response.m_msg << std::endl;
    }

    // get cfg_double
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_double"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_double success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingDouble", YSettingDouble, yDouble);
        std::cout << "cfg_double: " << yDouble->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_double failed: " << response.m_msg << std::endl;
    }

    // get cfg_double_array
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_double_array"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_double_array success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingDoubleArray", YSettingDoubleArray, yDoubleArray);
        for(int i = 0; i < yDoubleArray->d.size(); i++)
        {
            std::cout << "cfg_double_array[" << i << "] = " << yDoubleArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_double_array failed: " << response.m_msg << std::endl;
    }

    // get cfg_int
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_int"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_int success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingInt", YSettingInt, yInt);
        std::cout << "cfg_int: " << yInt->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_int failed: " << response.m_msg << std::endl;
    }

    // get cfg_int_array
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_int_array"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_int_array success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingIntArray", YSettingIntArray, yIntArray);
        for(int i = 0; i < yIntArray->d.size(); i++)
        {
            std::cout << "cfg_int_array[" << i << "] = " << yIntArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_int_array failed: " << response.m_msg << std::endl;
    }

    // get cfg_string
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_string"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_string success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingString", YSettingString, yStr);
        std::cout << "cfg_string: " << yStr->d << std::endl;
    }
    else
    {
        std::cout << "get cfg_string failed: " << response.m_msg << std::endl;
    }

    // get cfg_string_array
    response = server.request("/YomkSettings/get", YomkMkYStringPtr("cfg_string_array"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "get cfg_string_array success" << std::endl;
        YomkUnPackPkg(response.m_data, "YSettingStringArray", YSettingStringArray, yStrArray);
        for(int i = 0; i < yStrArray->d.size(); i++)
        {
            std::cout << "cfg_string_array[" << i << "] = " << yStrArray->d[i] << std::endl;
        }
    }
    else
    {
        std::cout << "get cfg_string_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_bool
    response = server.request("/YomkSettings/set", YomkMkYSettingBoolPtr("cfg_bool", true));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_bool success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool failed: " << response.m_msg << std::endl;
    }

    // set cfg_bool_array
    response = server.request("/YomkSettings/set", YomkMkYSettingBoolArrayPtr("cfg_bool_array", { false, false, false, true, true, true }));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_bool_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_double
    response = server.request("/YomkSettings/set", YomkMkYSettingDoublePtr("cfg_double", 3.1415926));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_double success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double failed: " << response.m_msg << std::endl;
    }

    // set cfg_double_array
    response = server.request("/YomkSettings/set", YomkMkYSettingDoubleArrayPtr("cfg_double_array", { 100.1, 200.1, 300.1, 400.1, 500.1, 600.1 }));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_double_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_int  
    response = server.request("/YomkSettings/set", YomkMkYSettingIntPtr("cfg_int", 10086));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_int success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int failed: " << response.m_msg << std::endl;
    }

    // set cfg_int_array
    response = server.request("/YomkSettings/set", YomkMkYSettingIntArrayPtr("cfg_int_array", { 1000, 2000, 3000, 4000, 5000, 6000 }));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_int_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_string
    response = server.request("/YomkSettings/set", YomkMkYSettingStringPtr("cfg_string", "1.0.3"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_string success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string failed: " << response.m_msg << std::endl;
    }

    // set cfg_string_array
    response = server.request("/YomkSettings/set", YomkMkYSettingStringArrayPtr("cfg_string_array", { "aa", "bb", "cc", "dd", "ee", "ff" }));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_string_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string_array failed: " << response.m_msg << std::endl;
    }

    // save settings
    response = server.request("/YomkSettings/save", YomkMkYStringPtr(settingsPath));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "save success" << std::endl;
    }
    else
    {
        std::cout << "save failed: " << response.m_msg << std::endl;
    }
    
    std::cout << "test settings completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
