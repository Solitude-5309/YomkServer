#include "FileLogger.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "yomk/YomkDiag.h"
namespace fs = std::filesystem;

// 初始化列表须按 FileLogger.h 声明序书写(-Wreorder)
FileLogger::FileLogger()
    : m_name("main"),
      m_showTraceLog(true),
      m_showDebugLog(true),
      m_showInfoLog(true),
      m_showWarnLog(true),
      m_showErrorLog(true),
      m_showFatalLog(true)
{}

FileLogger::~FileLogger()
{
    write();
}

bool FileLogger::init()
{
    std::string logFilePath = m_dir + "/" + m_name + ".log";

    try
    {
        fs::path path(logFilePath);
        fs::path dir = path.parent_path();

        if (!dir.empty() && !fs::exists(dir))
        {
            fs::create_directories(dir);
        }

        std::ofstream logFile(logFilePath);
        if (logFile.is_open())
        {
            logFile.close();
        }
        else
        {
            YOMK_DIAG_ERROR("create log file failed: " + logFilePath);
            logFile.close();
            return false;
        }
    }
    catch (const fs::filesystem_error& e)
    {
        YOMK_DIAG_ERROR("init file logger fs error: " + logFilePath + ", " + e.what());
        return false;
    }
    catch (const std::exception& e)
    {
        YOMK_DIAG_ERROR("init file logger error: " + logFilePath + ", " + e.what());
        return false;
    }
    return true;
}

void FileLogger::setLevelOn(LogLevel logLevel, bool on)
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
            // unknown 级别按 Info 门控(与 log() 降级语义一致)
            m_showInfoLog.store(on);
            break;
    }
}

bool FileLogger::isLevelOn(LogLevel logLevel) const
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

void FileLogger::log(LogLevel logLevel, const std::string& tag, const std::string& log)
{
    // 级别门控随 sink 自持:关断级别直接丢弃不落缓冲(unknown 级别按 Info 门控,
    // 与下方 default 降级语义一致);判定在缓冲锁外完成,atomic 读无新锁,
    // 位置在取时间戳之前避免白算
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

    // tag 由日志器统一拼接:空 tag 不加前缀
    const std::string tagPrefix = tag.empty() ? "" : "[" + tag + "] ";

    std::lock_guard<std::mutex> lock(m_logBufferMutex);
    auto appendLine = [this, &timeStr, &tagPrefix, &log](const char* levelTag)
    {
        m_logBuffer += timeStr;
        m_logBuffer += levelTag;
        m_logBuffer += tagPrefix;
        m_logBuffer += log;
        m_logBuffer += '\n';
    };
    switch (logLevel)
    {
        case LogLevel::Trace:
            appendLine(" " YOMK_DIAG_TAG_TRACE " ");
            break;
        case LogLevel::Debug:
            appendLine(" " YOMK_DIAG_TAG_DEBUG " ");
            break;
        case LogLevel::Info:
            appendLine(" " YOMK_DIAG_TAG_INFO " ");
            break;
        case LogLevel::Warn:
            appendLine(" " YOMK_DIAG_TAG_WARN " ");
            break;
        case LogLevel::Error:
            appendLine(" " YOMK_DIAG_TAG_ERROR " ");
            break;
        case LogLevel::Fatal:
            appendLine(" " YOMK_DIAG_TAG_FATAL " ");
            break;
        default:
            YOMK_DIAG_WARN("unknown log level " + std::to_string(logLevel) + ", fallback to Info");
            appendLine(" " YOMK_DIAG_TAG_INFO " ");
            break;
    }
}

void FileLogger::write()
{
    std::lock_guard<std::mutex> lock(m_logBufferMutex);
    // 直接落盘成员缓冲:避免 str() 全量拷贝——MB 级临时串走 mmap 逐页缺页,开销远大于拷贝本身
    if (m_logBuffer.empty())
    {
        return;
    }

    std::ofstream logFile(m_dir + "/" + m_name + ".log", std::ios_base::app);
    if (!logFile.is_open())
    {
        // 开文件失败直接返回,不清缓冲(内容不丢,TestYomkLoggerDirect 断言此语义)
        YOMK_DIAG_ERROR("open log file failed: " + m_dir + "/" + m_name + ".log");
        return;
    }
    logFile << m_logBuffer;
    logFile.close();

    m_logBuffer.clear();
}
