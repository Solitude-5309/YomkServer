#include <iostream>
#include "YomkAPI.h"
#include <filesystem>
namespace fs = std::filesystem;
YomkResponse func1(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkResponse(YomkResponse::eInvalid, "YString is null");
    }

    std::cout << "func1 called with data: " << yString->d << std::endl;

    return {YomkResponse::eOk, "func1 success. "};
}

int main(int argc, char *argv[])
{
    fs::path exePath = fs::canonical(argv[0]);
    fs::path settingsPath = exePath.parent_path().parent_path() / "Test" / "YomkServer" / "Settings" / "settings.json";
    std::cout << "Settings path: " << settingsPath << std::endl;
    
    YOMK_INIT(std::make_shared<YomkServer>(), { 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    
    YomkResponse response = YOMK_FUNCTIONPOOL_REGISTER("func1", func1);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "register func1 success" << std::endl;
    }
    else
    {
        std::cout << "register func1 failed: " << response.m_msg << std::endl;
    }

    response = YOMK_FUNCTIONPOOL_CALL("func1", YomkMkYStringPtr(settingsPath.string()));
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
