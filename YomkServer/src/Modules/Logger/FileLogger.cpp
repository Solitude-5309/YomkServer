#include "FileLogger.h"
#include "YomkDefine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <array>
namespace fs = std::filesystem;

// P1-c（LG2 修复）：线程安全本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]"；
// std::localtime 返回静态 tm 非线程安全，POSIX 用 localtime_r，Windows 用 localtime_s
// P4-b（LG4 修复）：原实现每行构造 std::stringstream + std::put_time（含 locale/facet 查找
// 与流缓冲区分配），是 file 日志热路径的主要单条开销 → 改定长缓冲 snprintf；
// 格式逐字节保持（等价性由 TestYomkLoggerDirect/Lifecycle/Concurrency 的 checkTimeFormat
// 断言 + TestYomkLoggerStress S1-A2 逐行校验共同兜底）
// P4-f（登记留 LG5）：本函数与 ConsoleLogger.cpp 的同名实现为重复代码，本次两处同改，
// 抽取公共内部头留 LG5 评估
static std::string localTimeFormatted()
{
    // tm_year 为自 1900 起的年数、tm_mon 为 0 基月份；毫秒由 1000 取模得到
    constexpr int kTmYearBase = 1900;
    constexpr int kTmMonBase = 1;
    constexpr int kMillisPerSecond = 1000;
    // 实际最长 "[9999-12-31 23:59:59.999]" = 25 字符 + NUL；但 std::tm 字段为无约束 int，
    // 编译器无法证明取值范围（%d 最宽 11 字节），故按 11 × 7 段 + 8 个字面字符 + NUL = 86
    // 取 96：既杜绝 -Wformat-truncation，也仍是栈上定长缓冲、不引入任何堆分配。
    // 用 std::array 而非 char[96]：内存布局与开销完全相同，但满足 clang-tidy
    // cppcoreguidelines-avoid-c-arrays，把本次新代码的抑制面收窄到仅剩 vararg 一条
    constexpr size_t kTimeBufSize = 96;

    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
              kMillisPerSecond;
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &nowTime);
#else
    localtime_r(&nowTime, &tmBuf);
#endif
    std::array<char, kTimeBufSize> buf{};
    // snprintf 是此处唯一能一次成型写出各段定宽零填充的设施——std::put_time 正是 P4-b 要
    // 移除的开销源，std::format 不属 C++17，std::to_chars 则需逐段手写填充。格式串为编译期
    // 字面量（无格式注入），缓冲为栈上定长 std::array 且显式传 size()（无越界），
    // 故此处 vararg 是受控且必要的
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(buf.data(), buf.size(), "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
                  tmBuf.tm_year + kTmYearBase, tmBuf.tm_mon + kTmMonBase, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf.data());
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
