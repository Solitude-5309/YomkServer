#include "FileLogger.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "YomkDefine.h"
#include "YomkLogTime.h"
namespace fs = std::filesystem;

// 初始化列表须按 FileLogger.h 声明序书写（-Wreorder）
FileLogger::FileLogger()
    : m_name("MainLogger"),
      m_showDebugLog(true),
      m_showInfoLog(true),
      m_showWarnLog(true),
      m_showErrorLog(true)
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
            YOMK_ERR_POS_LOG("create log file failed: " + logFilePath);
            logFile.close();
            return false;
        }
    }
    catch (const fs::filesystem_error& e)
    {
        YOMK_ERR_POS_LOG("init file logger fs error: " + logFilePath + ", " + e.what());
        return false;
    }
    catch (const std::exception& e)
    {
        YOMK_ERR_POS_LOG("init file logger error: " + logFilePath + ", " + e.what());
        return false;
    }
    return true;
}

void FileLogger::setLevelOn(ELogLevel logLevel, bool on)
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

bool FileLogger::isLevelOn(ELogLevel logLevel) const
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

void FileLogger::log(ELogLevel logLevel, const std::string& tag, const std::string& log)
{
    // 级别门控随 sink 自持：关断级别直接丢弃不落缓冲（unknown 级别按 Info 门控，
    // 与下方 default 降级语义一致）；判定在缓冲锁外完成，atomic 读无新锁，
    // 位置在取时间戳之前避免白算
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

    // tag 由日志器统一拼接（替代旧 API 层拼接）：空 tag 不加前缀，与旧行为一致
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
        case eDebug:
            appendLine(" [Debug] ");
            break;
        case eInfo:
            appendLine(" [Info ] ");
            break;
        case eWarn:
            appendLine(" [Warn ] ");
            break;
        case eError:
            appendLine(" [Error] ");
            break;
        default:
            YOMK_ERR_POS_LOG("Unknown log level: " + std::to_string(logLevel) + ", using Info level instead.");
            appendLine(" [Info ] ");
            break;
    }
}

void FileLogger::write()
{
    std::lock_guard<std::mutex> lock(m_logBufferMutex);
    // 直接落盘成员缓冲：避免 str() 全量拷贝——MB 级临时串走 mmap 逐页缺页，开销远大于拷贝本身
    if (m_logBuffer.empty())
    {
        return;
    }

    std::ofstream logFile(m_dir + "/" + m_name + ".log", std::ios_base::app);
    if (!logFile.is_open())
    {
        // 开文件失败直接返回、不清缓冲（内容不丢，既有语义由 TestYomkLoggerDirect 断言）
        YOMK_ERR_POS_LOG("open log file failed: " + m_dir + "/" + m_name + ".log");
        return;
    }
    logFile << m_logBuffer;
    logFile.close();

    m_logBuffer.clear();
}
