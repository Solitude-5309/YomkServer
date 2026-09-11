#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>

// Logger 模块内部头（不对外安装）：本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]" 的单一实现。
// localtime_r/_s 保证线程安全（std::localtime 返回静态 tm 非线程安全）；
// snprintf 定长栈缓冲替代 stringstream + put_time（热路径主要单条开销）。
// inline 保证跨 TU 单一定义（ODR），ConsoleLogger.cpp 与 FileLogger.cpp 各自内联展开；
// yomkLog 前缀避免与其他 TU 的全局符号冲突
inline std::string yomkLogLocalTimeFormatted()
{
    // tm_year 为自 1900 起的年数、tm_mon 为 0 基月份；毫秒由 1000 取模得到
    constexpr int kTmYearBase = 1900;
    constexpr int kTmMonBase = 1;
    constexpr int kMillisPerSecond = 1000;
    // 按 %d 最宽 11 字节 × 7 段 + 字面字符 + NUL 取 96：杜绝 -Wformat-truncation，栈上定长无堆分配；
    // std::array 而非 char[]：布局开销相同，满足 avoid-c-arrays
    constexpr size_t kTimeBufSize = 96;

    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % kMillisPerSecond;
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &nowTime);
#else
    localtime_r(&nowTime, &tmBuf);
#endif
    std::array<char, kTimeBufSize> buf{};
    // 格式串为编译期字面量（无注入）、定长缓冲显式传 size()（无越界），vararg 受控且必要
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(
        buf.data(),
        buf.size(),
        "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
        tmBuf.tm_year + kTmYearBase,
        tmBuf.tm_mon + kTmMonBase,
        tmBuf.tm_mday,
        tmBuf.tm_hour,
        tmBuf.tm_min,
        tmBuf.tm_sec,
        static_cast<int>(ms.count()));
    return std::string(buf.data());
}
