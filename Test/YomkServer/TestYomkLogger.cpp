#include <iostream>
#include "YomkAPI.h"

#include <filesystem>
namespace fs = std::filesystem;

bool consoleLogProxy(const yomk::Log& log)
{
    switch (log.m_level)
    {
    case yomk::Log::eInfo:
        std::cout << "into console log proxy: [INFO ] " << "[" << log.m_logger << "]"<< log.m_log << std::endl;
        break;
    case yomk::Log::eWarn:
        std::cout << "into console log proxy: [WARN ] " << "[" << log.m_logger << "]"<< log.m_log << std::endl;
        break;
    case yomk::Log::eError:
        std::cout << "into console log proxy: [ERROR] " << "[" << log.m_logger << "]"<< log.m_log << std::endl;
        break;
    case yomk::Log::eDebug:
        std::cout << "into console log proxy: [DEBUG] " << "[" << log.m_logger << "]"<< log.m_log << std::endl;
        break;
    default:
        break;
    }
    return true;
}

int main(int argc, char *argv[])
{
    fs::path exePath = fs::canonical(argv[0]);
    fs::path logDir = exePath.parent_path().parent_path() / "Test" / "YomkServer" / "YomkLog";
    std::cout << "Log dir: " << logDir << std::endl;

    YOMK_INIT(std::make_shared<YomkServer>(), { 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    
    YomkResponse response;

    // 关闭控制台INFO日志
    YOMK_OFF_CONSOLE_LOG_INFO();
    // 关闭控制台WARN日志
    YOMK_OFF_CONSOLE_LOG_WARN();
    // 关闭控制台ERROR日志
    YOMK_OFF_CONSOLE_LOG_ERROR();
    // 关闭控制台DEBUG日志
    YOMK_OFF_CONSOLE_LOG_DEBUG();

    response = YOMK_INFO("test", " console log info. ", 1);
    response = YOMK_WARN("test", " console log warn. ", 2);
    response = YOMK_ERROR("test", " console log error. ", 3);
    response = YOMK_DEBUG("test", " console log debug. ", 4);

    // 开启控制台INFO日志
    YOMK_ON_CONSOLE_LOG_INFO();
    // 开启控制台WARN日志
    YOMK_ON_CONSOLE_LOG_WARN();
    // 开启控制台ERROR日志
    YOMK_ON_CONSOLE_LOG_ERROR();
    // 开启控制台DEBUG日志
    YOMK_ON_CONSOLE_LOG_DEBUG();

    // 设置控制台日志代理
    response = YOMK_SET_CONSOLE_LOG_PROXY(consoleLogProxy);

    // 创建新的控制台日志器
    response = YOMK_INFO("test", " console log info. ", 5);
    response = YOMK_WARN("test", " console log warn. ", 6);
    response = YOMK_ERROR("test", " console log error. ", 7);
    response = YOMK_DEBUG("test", " console log debug. ", 8);
    
    // 创建新的控制台日志器
    response = YOMK_INFO_TAG("new_console_logger", "test", " new_console_logger log info. ", 1);
    response = YOMK_WARN_TAG("new_console_logger", "test", " new_console_logger log warn. ", 2);
    response = YOMK_ERROR_TAG("new_console_logger", "test", " new_console_logger log error. ", 3); 
    response = YOMK_DEBUG_TAG("new_console_logger", "test", " new_console_logger log debug. ", 4);

    // 创建新的文件日志器
    response = YOMK_FILE_LOG_CREATE(logDir.string(), "new_file_logger");
    
    // 创建新的文件日志器
    response = YOMK_FILE_INFO("new_file_logger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN("new_file_logger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR("new_file_logger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG("new_file_logger", "test", " new_file_logger log debug. ", 4);

    // 创建新的文件日志器
    response = YOMK_FILE_INFO_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log info. ", 1);
    response = YOMK_FILE_WARN_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log warn. ", 2);
    response = YOMK_FILE_ERROR_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log error. ", 3);
    response = YOMK_FILE_DEBUG_TAG("new_file_logger", "TestLogger", "test", " new_file_logger log debug. ", 4);

    // 写文件日志
    response = YOMK_FILE_LOG_WRITE("new_file_logger");
    
    std::cout << "test YomkLogger completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
