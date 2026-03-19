#include <iostream>
#include "YomkAPI.h"

#include <filesystem>
namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
    fs::path exePath = fs::canonical(argv[0]);
    fs::path logDir = exePath.parent_path().parent_path() / "Test" / "YomkServer" / "YomkLog";
    std::cout << "Log dir: " << logDir << std::endl;

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

    response = YOMK_INFO("test console log info. ");
    response = YOMK_WARN("test console log warn. ");
    response = YOMK_ERROR("test console log error. ");
    response = YOMK_DEBUG("test console log debug. ");
    
    response = YOMK_INFO("test new_console_logger log info. ", "new_console_logger");
    response = YOMK_WARN("test new_console_logger log warn. ", "new_console_logger");
    response = YOMK_ERROR("test new_console_logger log error. ", "new_console_logger"); 
    response = YOMK_DEBUG("test new_console_logger log debug. ", "new_console_logger");
    
    response = YomkAPI::FILE_LOG_CREATE(logDir, "new_file_logger");
    
    response = YOMK_FILE_INFO("new_file_logger", "test new_file_logger log info. ");
    response = YOMK_FILE_WARN("new_file_logger", "test new_file_logger log warn. ");
    response = YOMK_FILE_ERROR("new_file_logger", "test new_file_logger log error. ");
    response = YOMK_FILE_DEBUG("new_file_logger", "test new_file_logger log debug. ");

    response = YomkAPI::FILE_LOG_WRITE("new_file_logger");
    
    std::cout << "test YomkLogger completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
