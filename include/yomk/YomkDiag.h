#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

// 本地时间戳 "[YYYY-MM-DD HH:MM:SS.mmm]" 的单一实现,
// 诊断通道与 Logger 模块共用.
// localtime_r/_s 保证线程安全(std::localtime 返回静态 tm 非线程安全);
// snprintf 定长栈缓冲, 规避 stringstream + put_time 的热路径单条开销.
// inline 保证跨 TU 单一定义(ODR),各包含方各自内联展开;
// yomkLog 前缀避免与其他 TU 的全局符号冲突
inline std::string yomkLogLocalTimeFormatted()
{
    // tm_year 为自 1900 起的年数,tm_mon 为 0 基月份;毫秒由 1000 取模得到
    constexpr int kTmYearBase = 1900;
    constexpr int kTmMonBase = 1;
    constexpr int kMillisPerSecond = 1000;
    // 按 %d 最宽 11 字节 × 7 段 + 字面字符 + NUL 取 96:杜绝 -Wformat-truncation,栈上定长无堆分配;
    // std::array 而非 char[]:布局开销相同,满足 avoid-c-arrays
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
    // 格式串为编译期字面量(无注入),定长缓冲显式传 size()(无越界),vararg 受控且必要
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

// 级别标签常量:Logger 与诊断通道共用,方括号内宽严格定 5(总宽 7)保证纵向对齐;
// yomkDiagTagWidth + static_assert 编译期逐字面量把关,错宽直接编译失败,杜绝手滑破对齐
// (数组引用模板取字面量声明长度含 '\0',仅接受字符串字面量,无指针运算)
template <std::size_t N>
constexpr std::size_t yomkDiagTagWidth(const char (&tag)[N])  // NOLINT(cppcoreguidelines-avoid-c-arrays) 字符串字面量引用为唯一取长手段
{
    return N - 1;
}

#define YOMK_DIAG_TAG_TRACE "[TRACE]"
#define YOMK_DIAG_TAG_DEBUG "[DEBUG]"
#define YOMK_DIAG_TAG_INFO "[INFO ]"
#define YOMK_DIAG_TAG_WARN "[WARN ]"
#define YOMK_DIAG_TAG_ERROR "[ERROR]"
#define YOMK_DIAG_TAG_FATAL "[FATAL]"

// 诊断级别数值:yomkDiagSetMinLevel 的入参口径,宏族内部也按此判定是否输出
constexpr int YOMK_DIAG_LEVEL_TRACE = 0;
constexpr int YOMK_DIAG_LEVEL_DEBUG = 1;
constexpr int YOMK_DIAG_LEVEL_INFO = 2;
constexpr int YOMK_DIAG_LEVEL_WARN = 3;
constexpr int YOMK_DIAG_LEVEL_ERROR = 4;
constexpr int YOMK_DIAG_LEVEL_FATAL = 5;

// 诊断通道最小级别阈值(运行期可调):低于阈值的诊断整条丢弃,消息表达式在阈值判断之后求值,
// 抑制时零字符串构造开销.默认 WARN——INFO/DEBUG 静默,WARN 及以上输出;
// 降噪/开发期排查可调低阈值放行 INFO(如热替换提示),调高可只留 ERROR.
inline std::atomic<int>& yomkDiagMinLevelRef()
{
    static std::atomic<int> minLevel{YOMK_DIAG_LEVEL_WARN};
    return minLevel;
}

inline bool yomkDiagLevelEnabled(int level)
{
    return level >= yomkDiagMinLevelRef().load(std::memory_order_relaxed);
}

// 设置诊断最小级别(入参取 YOMK_DIAG_LEVEL_* 常量);进程内任意线程随时可调
inline void yomkDiagSetMinLevel(int level)
{
    yomkDiagMinLevelRef().store(level, std::memory_order_relaxed);
}

/**
 * @brief Framework self-diagnostic channel: raw stdout fallback when the logger is unavailable
 * Emits four segments (timestamp/level/tag/location) with the tag fixed to Yomk; the macro family
 * extends by YOMK_DIAG_<LEVEL>, each macro takes one message and prints it immediately
 * (no thread-safe ordering, diagnostic use only). Output is gated by the runtime minimum level
 * (default WARN; adjust via yomkDiagSetMinLevel, suppressed messages cost no string construction)
 */
// @zh: 框架自诊断通道：logger 不可用时的裸 stdout 兜底
// @zh: 输出时间戳/等级/标签/位置四段，tag 固定 Yomk；
// @zh: 宏族按 YOMK_DIAG_<级别> 扩展，每宏接收一条消息并立即输出（非线程安全序，仅诊断用途）
// @zh: 输出受运行期最小级别门控（默认 WARN；经 yomkDiagSetMinLevel 调整，被抑制的消息零字符串构造开销）
#define YOMK_DIAG_LOG_IMPL(levelTag, level, msg)                                                                              \
    do                                                                                                                        \
    {                                                                                                                         \
        static_assert(yomkDiagTagWidth(levelTag) == 7, "diag level tag must be 7 chars wide to keep level columns aligned");  \
        if (yomkDiagLevelEnabled(level))                                                                                      \
        {                                                                                                                     \
            std::cout << yomkLogLocalTimeFormatted() << " " << levelTag << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "]"  \
                      << " [" << __func__ << "] " << msg << std::endl;                                                        \
        }                                                                                                                     \
    } while (0)

#define YOMK_DIAG_TRACE(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_TRACE, YOMK_DIAG_LEVEL_TRACE, msg)
#define YOMK_DIAG_DEBUG(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_DEBUG, YOMK_DIAG_LEVEL_DEBUG, msg)
#define YOMK_DIAG_INFO(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_INFO, YOMK_DIAG_LEVEL_INFO, msg)
#define YOMK_DIAG_WARN(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_WARN, YOMK_DIAG_LEVEL_WARN, msg)
#define YOMK_DIAG_ERROR(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_ERROR, YOMK_DIAG_LEVEL_ERROR, msg)
#define YOMK_DIAG_FATAL(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_FATAL, YOMK_DIAG_LEVEL_FATAL, msg)
