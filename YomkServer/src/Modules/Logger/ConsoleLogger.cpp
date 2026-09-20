#include "ConsoleLogger.h"

#include <iostream>

#include "YomkDefine.h"
#include "YomkLogTime.h"

ConsoleLogger::ConsoleLogger() : m_name("MainLogger") {}

ConsoleLogger::~ConsoleLogger() {}

void ConsoleLogger::log(ELogLevel logLevel, const std::string& tag, const std::string& log)
{
    std::string timeStr = yomkLogLocalTimeFormatted();

    std::lock_guard<std::mutex> lock(m_mutex);

    // tag 优先显示；空 tag 回退日志器名，保持旧行为
    const std::string& tagShown = tag.empty() ? m_name : tag;

    switch (logLevel)
    {
        case eDebug:
            std::cout << timeStr << " [Debug] [" << tagShown << "] " << log << std::endl;
            break;
        case eInfo:
            std::cout << timeStr << " [Info ] [" << tagShown << "] " << log << std::endl;
            break;
        case eWarn:
            std::cout << timeStr << " [Warn ] [" << tagShown << "] " << log << std::endl;
            break;
        case eError:
            std::cout << timeStr << " [Error] [" << tagShown << "] " << log << std::endl;
            break;
        default:
            YOMK_ERR_POS_LOG("Unknown log level: " + std::to_string(logLevel) + ", using Info level instead.");
            std::cout << timeStr << " [Info ] [" << tagShown << "] " << log << std::endl;
            break;
    }
}
