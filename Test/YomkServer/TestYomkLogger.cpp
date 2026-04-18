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

    YOMK_OFF_CONSOLE_LOG_INFO();
    YOMK_OFF_CONSOLE_LOG_WARN();
    YOMK_OFF_CONSOLE_LOG_ERROR();
    YOMK_OFF_CONSOLE_LOG_DEBUG();
    response = YOMK_INFO("test", " console log info. ", 1);
    response = YOMK_WARN("test", " console log warn. ", 2);
    response = YOMK_ERROR("test", " console log error. ", 3);
    response = YOMK_DEBUG("test", " console log debug. ", 4);
    YOMK_ON_CONSOLE_LOG_INFO();
    YOMK_ON_CONSOLE_LOG_WARN();
    YOMK_ON_CONSOLE_LOG_ERROR();
    YOMK_ON_CONSOLE_LOG_DEBUG();
    response = YOMK_INFO("test", " console log info. ", 5);
    response = YOMK_WARN("test", " console log warn. ", 6);
    response = YOMK_ERROR("test", " console log error. ", 7);
    response = YOMK_DEBUG("test", " console log debug. ", 8);
    response = YOMK_INFO_TAG("new_console_logger", "test", " new_console_logger log info. ", 1);
    response = YOMK_WARN_TAG("new_console_logger", "test", " new_console_logger log warn. ", 2);
    response = YOMK_ERROR_TAG("new_console_logger", "test", " new_console_logger log error. ", 3); 
    response = YOMK_DEBUG_TAG("new_console_logger", "test", " new_console_logger log debug. ", 4);
    
    response = YomkAPI::FILE_LOG_CREATE(logDir.string(), "new_file_logger");
    
    response = YOMK_FILE_INFO("new_file_logger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN("new_file_logger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR("new_file_logger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG("new_file_logger", "test", " new_file_logger log debug. ", 4);

    response = YOMK_FILE_INFO_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log debug. ", 4);

    response = YomkAPI::FILE_LOG_WRITE("new_file_logger");
    
    std::cout << "test YomkLogger completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
