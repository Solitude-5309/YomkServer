#include "ConsoleLogger.h"

#include <iostream>

#include "yomk/YomkDiag.h"

// 类级共享锁定义：跨实例行级原子的唯一互斥点（见 ConsoleLogger.h 成员注释）
std::mutex ConsoleLogger::s_mutex;

// 初始化列表须按 ConsoleLogger.h 声明序书写（-Wreorder）
ConsoleLogger::ConsoleLogger()
    : m_name("main"),
      m_showTraceLog(true),
      m_showDebugLog(true),
      m_showInfoLog(true),
      m_showWarnLog(true),
      m_showErrorLog(true),
      m_showFatalLog(true)
{}

ConsoleLogger::~ConsoleLogger() {}

void ConsoleLogger::setLevelOn(LogLevel logLevel, bool on)
{
    switch (logLevel)
    {
        case LogLevel::Trace:
            m_showTraceLog.store(on);
            break;
        case LogLevel::Debug:
            m_showDebugLog.store(on);
            break;
        case LogLevel::Info:
            m_showInfoLog.store(on);
            break;
        case LogLevel::Warn:
            m_showWarnLog.store(on);
            break;
        case LogLevel::Error:
            m_showErrorLog.store(on);
            break;
        case LogLevel::Fatal:
            m_showFatalLog.store(on);
            break;
        default:
            // unknown 级别按 Info 门控（与 log() 降级语义一致）
            m_showInfoLog.store(on);
            break;
    }
}

bool ConsoleLogger::isLevelOn(LogLevel logLevel) const
{
    switch (logLevel)
    {
        case LogLevel::Trace:
            return m_showTraceLog.load();
        case LogLevel::Debug:
            return m_showDebugLog.load();
        case LogLevel::Info:
            return m_showInfoLog.load();
        case LogLevel::Warn:
            return m_showWarnLog.load();
        case LogLevel::Error:
            return m_showErrorLog.load();
        case LogLevel::Fatal:
            return m_showFatalLog.load();
        default:
            return m_showInfoLog.load();
    }
}

void ConsoleLogger::log(LogLevel logLevel, const std::string& tag, const std::string& log)
{
    // 级别门控随 sink 自持：关断级别直接丢弃不输出（unknown 级别按 Info 门控，
    // 与下方 default 降级语义一致）；判定在类级锁外完成，atomic 读无新锁
    switch (logLevel)
    {
        case LogLevel::Trace:
            if (!m_showTraceLog.load())
                return;
            break;
        case LogLevel::Debug:
            if (!m_showDebugLog.load())
                return;
            break;
        case LogLevel::Info:
            if (!m_showInfoLog.load())
                return;
            break;
        case LogLevel::Warn:
            if (!m_showWarnLog.load())
                return;
            break;
        case LogLevel::Error:
            if (!m_showErrorLog.load())
                return;
            break;
        case LogLevel::Fatal:
            if (!m_showFatalLog.load())
                return;
            break;
        default:
            if (!m_showInfoLog.load())
                return;
            break;
    }

    std::string timeStr = yomkLogLocalTimeFormatted();

    // tag 优先显示；空 tag 回退日志器名
    const std::string& tagShown = tag.empty() ? m_name : tag;

    // 类级共享锁下整条 << 链直写：跨实例行级原子。stdout 底层 stdio 锁本已逐片段
    // 串行（未调 sync_with_stdio(false)），显式行级锁仅收编互斥粒度，不新增串行化
    // 资源；不留中间缓冲——锁外 ostringstream 拼行形态实测为负优化
    // （S1E -28%：三次堆分配 + str() 整行拷贝 ~0.5μs/条），故直写
    std::lock_guard<std::mutex> lock(s_mutex);
    switch (logLevel)
    {
        case LogLevel::Trace:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_TRACE << " [" << tagShown << "] " << log << std::endl;
            break;
        case LogLevel::Debug:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_DEBUG << " [" << tagShown << "] " << log << std::endl;
            break;
        case LogLevel::Info:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_INFO << " [" << tagShown << "] " << log << std::endl;
            break;
        case LogLevel::Warn:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_WARN << " [" << tagShown << "] " << log << std::endl;
            break;
        case LogLevel::Error:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_ERROR << " [" << tagShown << "] " << log << std::endl;
            break;
        case LogLevel::Fatal:
            std::cout << timeStr << " " << YOMK_DIAG_TAG_FATAL << " [" << tagShown << "] " << log << std::endl;
            break;
        default:
            YOMK_DIAG_ERROR("Unknown log level: " + std::to_string(logLevel) + ", using Info level instead.");
            std::cout << timeStr << " " << YOMK_DIAG_TAG_INFO << " [" << tagShown << "] " << log << std::endl;
            break;
    }
}
