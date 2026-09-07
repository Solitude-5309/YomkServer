#include "FileLogger.h"
#include "YomkDefine.h"
#include <iostream>
#include <fstream>
#include <filesystem>
// P4-f（LG5 修复）：时间戳实现抽取至模块内部头，本文件不再持有重复副本；
// 随之迁走 <chrono>/<cstdio>/<ctime>/<array>，并清除 P4-b 后已无用的 <iomanip>。
// 本文件从无 <sstream>：P4-a 前它由 FileLogger.h 为 std::stringstream 成员提供，
// P4-a 改成员为 std::string 后该包含已在头文件侧失效，本次一并清除
#include "YomkLogTime.h"
namespace fs = std::filesystem;

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
    std::string timeStr = yomkLogLocalTimeFormatted();

    std::lock_guard<std::mutex> lock(m_logBufferMutex);
    // P4-a（LG4 修复）：逐行追加到 std::string 成员缓冲。与原
    // m_logStream << timeStr << " [Info ] " << log << std::endl 逐字节等价
    // （std::endl 对字符串流仅追加 '\n'，其 flush 对 stringstream 无意义）
    auto appendLine = [this, &timeStr, &log](const char *levelTag)
    {
        m_logBuffer += timeStr;
        m_logBuffer += levelTag;
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
    // P4-a（LG4 修复）：原实现用 std::stringstream 缓冲，落盘必须 m_logStream.str() 取快照，
    // 且取了 2 次（先 .size() 判空、再 << 写盘）= 2 次全量拷贝。改为 std::string 成员缓冲后，
    // 判空与写盘直接复用成员本体：落盘路径零拷贝、零大块瞬时分配。
    // 为何不能只把 2 次 str() 减为 1 次：str() 产生的 MB 级临时串超 glibc mmap 阈值，
    // 走 mmap → 逐页缺页 + 释放时 munmap，该开销远大于拷贝本身；进程内首次 flush 实测
    // （LG4 微基准，2.05MB 缓冲）：原 2 次拷贝 9.26 ms / 单次快照 5.25 ms / 本实现 1.02 ms
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

    // 清空缓冲内容（等价原 m_logStream.str("")），下一次填充重新累积
    m_logBuffer.clear();
}
