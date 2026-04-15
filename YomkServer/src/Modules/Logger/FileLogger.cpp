#include "FileLogger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

FileLogger::FileLogger()
    : m_name("MainLogger")
{
}

FileLogger::~FileLogger()
{
    write();
}

void FileLogger::init()
{
    std::string logFilePath = m_dir + "/" + m_name + ".log";

    fs::path path(logFilePath);
    fs::path dir = path.parent_path();
    
    if (!dir.empty() && !fs::exists(dir)) 
    {
        fs::create_directories(dir);
    }

    std::ofstream logFile(logFilePath);
    if (logFile.is_open()) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "create log file success: " << logFilePath << std::endl;
        logFile.close();
    } 
    else 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "create log file failed: " << logFilePath << std::endl;
        logFile.close();
    }
}

void FileLogger::log(ELogLevel logLevel, const std::string &log)
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream timeStr;
    timeStr << std::put_time(std::localtime(&in_time_t), "[%Y-%m-%d %H:%M:%S.");
    timeStr << std::setfill('0') << std::setw(3) << ms.count() << "]";

    std::lock_guard<std::mutex> lock(m_logStreamMutex);
    switch(logLevel)
    {
        case eDebug:
            m_logStream << timeStr.str() << " [Debug] " << log << std::endl;
            break;
        case eInfo:
            m_logStream << timeStr.str() << " [Info ] " << log << std::endl;
            break;
        case eWarn:
            m_logStream << timeStr.str() << " [Warn ] " << log << std::endl;
            break;
        case eError:
            m_logStream << timeStr.str() << " [Error] " << log << std::endl;
            break;
        default:
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "Unknown log level: " << logLevel << ", using Info level instead." << std::endl;
            m_logStream << timeStr.str() << " [Info ] " << log << std::endl;
            break;
    }
}

void FileLogger::write()
{
    std::lock_guard<std::mutex> lock(m_logStreamMutex);
    if(m_logStream.str().size() > 0)
    {
        std::ofstream logFile(m_dir + "/" + m_name + ".log", std::ios_base::app);
        if (logFile.is_open()) 
        {
            logFile << m_logStream.str();
            logFile.close();
        } 
        else 
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "open log file failed: " << m_dir + "/" + m_name + ".log" << std::endl;
            logFile.close();
            return;
        }
        m_logStream.str("");
    }
}
