#include <iostream>
#include "YomkAPI.h"
#include <filesystem>
namespace fs = std::filesystem;
YomkResponse func1(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);

    if(!str)
    {
        return YomkResponse(YomkResponse::eInvalid, "string is null");
    }

    YOMK_DEBUG_TAG("func1", "func1 called with data: ", str->d);

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
        YOMK_DEBUG_TAG("main", "register func1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "register func1 failed: ", response.m_msg);
    }

    response = YOMK_FUNCTIONPOOL_CALL("func1", YomkMkPtr(string, settingsPath.string()));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "call func1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "call func1 failed: ", response.m_msg);
    }
    
    YOMK_DEBUG_TAG("main", "test YomkFunctionPool completed, any key to continue...");

    getchar();

    return 0;
}
