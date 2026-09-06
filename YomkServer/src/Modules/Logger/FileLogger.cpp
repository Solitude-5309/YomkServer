#include "FileLogger.h"
#include "YomkDefine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <ctime>
namespace fs = std::filesystem;

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

FileLogger::FileLogger()
    : m_name("MainLogger")
{
}

FileLogger::~FileLogger()
{
    write();
}

bool FileLogger::init()
{
    std::string logFilePath = m_dir + "/" + m_name + ".log";

    // P2-b（LG2 修复）：fs 异常不再穿透调用链，失败一律返回 false
    // 由 createFileLogger 转为 eNo 响应且不注册幽灵 logger
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
    catch (const fs::filesystem_error &e)
    {
        YOMK_ERR_POS_LOG("init file logger fs error: " + logFilePath + ", " + e.what());
        return false;
    }
    catch (const std::exception &e)
    {
        YOMK_ERR_POS_LOG("init file logger error: " + logFilePath + ", " + e.what());
        return false;
    }
    return true;
}

void FileLogger::log(ELogLevel logLevel, const std::string &log)
{
    std::string timeStr = localTimeFormatted();

    std::lock_guard<std::mutex> lock(m_logStreamMutex);
    switch (logLevel)
    {
    case eDebug:
        m_logStream << timeStr << " [Debug] " << log << std::endl;
        break;
    case eInfo:
        m_logStream << timeStr << " [Info ] " << log << std::endl;
        break;
    case eWarn:
        m_logStream << timeStr << " [Warn ] " << log << std::endl;
        break;
    case eError:
        m_logStream << timeStr << " [Error] " << log << std::endl;
        break;
    default:
        YOMK_ERR_POS_LOG("Unknown log level: " + std::to_string(logLevel) + ", using Info level instead.");
        m_logStream << timeStr << " [Info ] " << log << std::endl;
        break;
    }
}

void FileLogger::write()
{
    std::lock_guard<std::mutex> lock(m_logStreamMutex);
    if (m_logStream.str().size() > 0)
    {
        std::ofstream logFile(m_dir + "/" + m_name + ".log", std::ios_base::app);
        if (logFile.is_open())
        {
            logFile << m_logStream.str();
            logFile.close();
        }
        else
        {
            YOMK_ERR_POS_LOG("open log file failed: " + m_dir + "/" + m_name + ".log");
            logFile.close();
            return;
        }
        m_logStream.str("");
        m_logStream.clear();
    }
}
