#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>

// Logger 模块内部头（不对外安装）：本地时间戳格式化的单一实现
//
// P1-c（LG2 修复）：线程安全本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]"；
// std::localtime 返回静态 tm 非线程安全，POSIX 用 localtime_r，Windows 用 localtime_s
// P4-b（LG4 修复）：原实现每行构造 std::stringstream + std::put_time（含 locale/facet 查找
// 与流缓冲区分配），是 console/file 日志热路径的主要单条开销 → 改定长缓冲 snprintf；
// 格式逐字节保持（等价性由 TestYomkLoggerDirect/Lifecycle/Concurrency 的 checkTimeFormat
// 断言 + TestYomkLoggerStress S1-A2 逐行校验共同兜底）
// P4-f（LG5 修复）：本函数原为 ConsoleLogger.cpp 与 FileLogger.cpp 中两处逐字同构的 static
// 实现（LG4 修 P4-b 时被迫两处同改，是重复代码的真实维护成本），现抽取为本内部头的单一实现。
// 用 inline 而非 static：inline 保证跨 TU 的单一定义（ODR），两个 .cpp 各自内联展开，
// 调用点开销与原 static 完全等价；且不新增编译单元，无需改 YomkServer/CMakeLists.txt 的源列表。
// 函数名加 yomkLog 前缀：Logger 模块的类均在全局命名空间（无 namespace 包裹），
// 而 inline 函数具有外部链接，需唯一名避免与其他 TU 的符号冲突。
inline std::string yomkLogLocalTimeFormatted()
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
