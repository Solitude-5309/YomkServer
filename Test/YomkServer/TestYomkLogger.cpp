#include <iostream>
#include "YomkServer.h"
#include "YomkDefine.h"

int main(int argc, char *argv[])
{
    std::string logDir = argv[0] + std::string("/../YomkLog");

    YomkServer server;
    server.startService({ "/YomkLogger" });
    
    YomkRespond respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eInfo, "test console log info. "));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eWarn, "test console log warn. "));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eError, "test console log error. "));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eDebug, "test console log debug. "));
    
    respond = server.request("/YomkLogger/create_console_logger", YomkMkYStringPtr("new_console_logger"));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eInfo, "test new_console_logger log info. ", "new_console_logger"));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eWarn, "test new_console_logger log warn. ", "new_console_logger"));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eError, "test new_console_logger log error. ", "new_console_logger"));
    respond = server.request("/YomkLogger/console_log", YomkMkYLogPtr(YLog::eDebug, "test new_console_logger log debug. ", "new_console_logger"));

    respond = server.request("/YomkLogger/create_file_logger", YomkMkYLogFilePtr(logDir, "new_file_logger"));
    respond = server.request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eInfo, "test new_file_logger log info. ", "new_file_logger"));
    respond = server.request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eWarn, "test new_file_logger log warn. ", "new_file_logger"));
    respond = server.request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eError, "test new_file_logger log error. ", "new_file_logger"));
    respond = server.request("/YomkLogger/file_log", YomkMkYLogPtr(YLog::eDebug, "test new_file_logger log debug. ", "new_file_logger"));

    respond = server.request("/YomkLogger/write_file_log", YomkMkYStringPtr("new_file_logger"));
    
    std::cout << "test YomkLogger completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
