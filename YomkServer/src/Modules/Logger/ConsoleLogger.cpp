#include "ConsoleLogger.h"

#include <iostream>

#include "YomkDefine.h"
#include "YomkLogTime.h"

// 类级共享锁定义：跨实例行级原子的唯一互斥点（见 ConsoleLogger.h 成员注释）
std::mutex ConsoleLogger::s_mutex;

// 初始化列表须按 ConsoleLogger.h 声明序书写（-Wreorder）
ConsoleLogger::ConsoleLogger()
    : m_name("MainLogger"),
      m_showDebugLog(true),
      m_showInfoLog(true),
      m_showWarnLog(true),
      m_showErrorLog(true)
{}

ConsoleLogger::~ConsoleLogger() {}

void ConsoleLogger::setLevelOn(ELogLevel logLevel, bool on)
{
    switch (logLevel)
    {
        case eDebug:
            m_showDebugLog.store(on);
            break;
        case eInfo:
            m_showInfoLog.store(on);
            break;
        case eWarn:
            m_showWarnLog.store(on);
            break;
        case eError:
            m_showErrorLog.store(on);
            break;
        default:
            // unknown 级别按 Info 门控（与 log() 降级语义一致）
            m_showInfoLog.store(on);
            break;
    }
}

bool ConsoleLogger::isLevelOn(ELogLevel logLevel) const
{
    switch (logLevel)
    {
        case eDebug:
            return m_showDebugLog.load();
        case eInfo:
            return m_showInfoLog.load();
        case eWarn:
            return m_showWarnLog.load();
        case eError:
            return m_showErrorLog.load();
        default:
            return m_showInfoLog.load();
    }
}

void ConsoleLogger::log(ELogLevel logLevel, const std::string& tag, const std::string& log)
{
    // 级别门控随 sink 自持：关断级别直接丢弃不输出（unknown 级别按 Info 门控，
    // 与下方 default 降级语义一致）；判定在类级锁外完成，atomic 读无新锁
    switch (logLevel)
    {
        case eDebug:
            if (!m_showDebugLog.load())
                return;
            break;
        case eInfo:
            if (!m_showInfoLog.load())
                return;
            break;
        case eWarn:
            if (!m_showWarnLog.load())
                return;
            break;
        case eError:
            if (!m_showErrorLog.load())
                return;
            break;
        default:
            if (!m_showInfoLog.load())
                return;
            break;
    }

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
