#include "ConsoleLogger.h"

#include <iostream>

#include "YomkDefine.h"
#include "YomkLogTime.h"

// 类级共享锁定义：跨实例行级原子的唯一互斥点（见 ConsoleLogger.h 成员注释）
std::mutex ConsoleLogger::s_mutex;

ConsoleLogger::ConsoleLogger() : m_name("MainLogger") {}

ConsoleLogger::~ConsoleLogger() {}

void ConsoleLogger::log(ELogLevel logLevel, const std::string& tag, const std::string& log)
{
    std::string timeStr = yomkLogLocalTimeFormatted();

    // tag 优先显示；空 tag 回退日志器名，保持旧行为
    const std::string& tagShown = tag.empty() ? m_name : tag;

    // 类级共享锁下整条 << 链直写：跨实例行级原子。stdout 底层 stdio 锁本已逐片段
    // 串行（未调 sync_with_stdio(false)），显式行级锁仅收编互斥粒度，不新增串行化
    // 资源；不留中间缓冲——锁外 ostringstream 拼行形态经 P4-h 实测为负优化
    // （S1E -28%：三次堆分配 + str() 整行拷贝 ~0.5μs/条），已回退为直写
    std::lock_guard<std::mutex> lock(s_mutex);
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
