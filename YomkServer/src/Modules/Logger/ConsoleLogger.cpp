#include "ConsoleLogger.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

ConsoleLogger::ConsoleLogger()
    : m_name("MainLogger")
{
}

ConsoleLogger::~ConsoleLogger()
{
}

void ConsoleLogger::log(ELogLevel logLevel, const std::string &log)
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream timeStr;
    timeStr << std::put_time(std::localtime(&in_time_t), "[%Y-%m-%d %H:%M:%S.");
    timeStr << std::setfill('0') << std::setw(3) << ms.count() << "]";

    switch(logLevel)
    {
        case eDebug:
            std::cout << timeStr.str() << " [Debug] [" << m_name << "] " << log << std::endl;
            break;
        case eInfo:
            std::cout << timeStr.str() << " [Info ] [" << m_name << "] " << log << std::endl;
            break;
        case eWarn:
            std::cout << timeStr.str() << " [Warn ] [" << m_name << "] " << log << std::endl;
            break;
        case eError:
            std::cout << timeStr.str() << " [Error] [" << m_name << "] " << log << std::endl;
            break;
        default:
            break;
    }
}
