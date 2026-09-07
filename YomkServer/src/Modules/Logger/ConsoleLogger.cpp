#include "ConsoleLogger.h"
#include <iostream>
// P4-f（LG5 修复）：时间戳实现抽取至模块内部头，本文件不再持有重复副本；
// 随之迁走 <chrono>/<cstdio>/<ctime>/<array>，并清除 P4-b 后已无用的 <sstream>/<iomanip>
#include "YomkLogTime.h"
#include "YomkDefine.h"

ConsoleLogger::ConsoleLogger()
    : m_name("MainLogger")
{
}

ConsoleLogger::~ConsoleLogger()
{
}

void ConsoleLogger::log(ELogLevel logLevel, const std::string &log)
{
    std::string timeStr = yomkLogLocalTimeFormatted();

    std::lock_guard<std::mutex> lock(m_mutex);

    switch (logLevel)
    {
    case eDebug:
        std::cout << timeStr << " [Debug] [" << m_name << "] " << log << std::endl;
        break;
    case eInfo:
        std::cout << timeStr << " [Info ] [" << m_name << "] " << log << std::endl;
        break;
    case eWarn:
        std::cout << timeStr << " [Warn ] [" << m_name << "] " << log << std::endl;
        break;
    case eError:
        std::cout << timeStr << " [Error] [" << m_name << "] " << log << std::endl;
        break;
    default:
        YOMK_ERR_POS_LOG("Unknown log level: " + std::to_string(logLevel) + ", using Info level instead.");
        std::cout << timeStr << " [Info ] [" << m_name << "] " << log << std::endl;
        break;
    }
}
