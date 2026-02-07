#include <iostream>
#include "YomkAPI.h"

int main(int argc, char *argv[])
{
    std::string settingsPath = argv[0] + std::string("/../../Settings/settings.json");

    std::shared_ptr<YomkServer> server = std::make_shared<YomkServer>();
    server->startService({ 
        "/YomkSettings", 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });

    YOMK_INIT(server);

    // load settings
    YomkResponse response = YOMK_SETTINGS_LOAD(settingsPath);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "load success" << std::endl;
    }
    else
    {
        std::cout << "load failed: " << response.m_msg << std::endl;
    }
    // get cfg_bool
    bool cfg_bool = YOMK_SETTINGS_GET_BOOL("cfg_bool");
    std::cout << "cfg_bool: " << cfg_bool << std::endl;
    // get cfg_bool_array
    std::vector<bool> cfg_bool_array = YOMK_SETTINGS_GET_BOOL_ARRAY("cfg_bool_array");
    for(int i = 0; i < cfg_bool_array.size(); i++)
    {
        std::cout << "cfg_bool_array[" << i << "] = " << cfg_bool_array[i] << std::endl;
    }
    // get cfg_double
    double cfg_double = YOMK_SETTINGS_GET_DOUBLE("cfg_double");
    std::cout << "cfg_double: " << cfg_double << std::endl;
    // get cfg_double_array
    std::vector<double> cfg_double_array = YOMK_SETTINGS_GET_DOUBLE_ARRAY("cfg_double_array");
    for(int i = 0; i < cfg_double_array.size(); i++)
    {
        std::cout << "cfg_double_array[" << i << "] = " << cfg_double_array[i] << std::endl;
    }
    // get cfg_int
    int64_t cfg_int = YOMK_SETTINGS_GET_INT("cfg_int");
    std::cout << "cfg_int: " << cfg_int << std::endl;
    // get cfg_int_array
    std::vector<int> cfg_int_array = YOMK_SETTINGS_GET_INT_ARRAY("cfg_int_array");
    for(int i = 0; i < cfg_int_array.size(); i++)
    {
        std::cout << "cfg_int_array[" << i << "] = " << cfg_int_array[i] << std::endl;
    }
    // get cfg_string
    std::string cfg_string = YOMK_SETTINGS_GET_STRING("cfg_string");
    std::cout << "cfg_string: " << cfg_string << std::endl;
    // get cfg_string_array
    std::vector<std::string> cfg_string_array = YOMK_SETTINGS_GET_STRING_ARRAY("cfg_string_array");
    for(int i = 0; i < cfg_string_array.size(); i++)
    {
        std::cout << "cfg_string_array[" << i << "] = " << cfg_string_array[i] << std::endl;
    }

    // set cfg_bool
    response = YOMK_SETTINGS_SET_BOOL("cfg_bool", true);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_bool success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool failed: " << response.m_msg << std::endl;
    }

    // set cfg_bool_array
    response = YOMK_SETTINGS_SET_BOOL_ARRAY("cfg_bool_array", { false, false, false, true, true, true });
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_bool_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_bool_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_double
    response = YOMK_SETTINGS_SET_DOUBLE("cfg_double", 3.1415926);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_double success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double failed: " << response.m_msg << std::endl;
    }

    // set cfg_double_array
    response = YOMK_SETTINGS_SET_DOUBLE_ARRAY("cfg_double_array", { 100.1, 200.1, 300.1, 400.1, 500.1, 600.1 });
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_double_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_double_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_int
    response = YOMK_SETTINGS_SET_INT("cfg_int", 10086);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_int success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int failed: " << response.m_msg << std::endl;
    }

    // set cfg_int_array
    response = YOMK_SETTINGS_SET_INT_ARRAY("cfg_int_array", { 1000, 2000, 3000, 4000, 5000, 6000 });
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_int_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_int_array failed: " << response.m_msg << std::endl;
    }

    // set cfg_string
    response = YOMK_SETTINGS_SET_STRING("cfg_string", "1.0.3");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_string success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string failed: " << response.m_msg << std::endl;
    }

    // set cfg_string_array
    response = YOMK_SETTINGS_SET_STRING_ARRAY("cfg_string_array", { "aa", "bb", "cc", "dd", "ee", "ff" });
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "set cfg_string_array success" << std::endl;
    }
    else
    {
        std::cout << "set cfg_string_array failed: " << response.m_msg << std::endl;
    }

    // save settings
    response = YOMK_SETTINGS_SAVE(settingsPath);
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
