#include "ConsoleLogger.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include "YomkDefine.h"

// P1-c（LG2 修复）：线程安全本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]"；
// std::localtime 返回静态 tm 非线程安全，POSIX 用 localtime_r，Windows 用 localtime_s
static std::string localTimeFormatted()
{
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &nowTime);
#else
    localtime_r(&nowTime, &tmBuf);
#endif
    std::stringstream timeStr;
    timeStr << std::put_time(&tmBuf, "[%Y-%m-%d %H:%M:%S.");
    timeStr << std::setfill('0') << std::setw(3) << ms.count() << "]";
    return timeStr.str();
}

ConsoleLogger::ConsoleLogger()
    : m_name("MainLogger")
{
}

ConsoleLogger::~ConsoleLogger()
{
}

void ConsoleLogger::log(ELogLevel logLevel, const std::string &log)
{
    std::string timeStr = localTimeFormatted();

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
