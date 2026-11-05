#pragma once

#include <cstddef>
#include <iostream>

#include "YomkLogTime.h"

// 级别标签常量：Logger 与诊断通道共用，方括号内宽严格定 5（总宽 7）保证纵向对齐；
// yomkDiagTagWidth + static_assert 编译期逐字面量把关，错宽直接编译失败，杜绝手滑破对齐
// （数组引用模板取字面量声明长度含 '\0'，仅接受字符串字面量，无指针运算）
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

// 框架自诊断通道（logger 不可用时的裸 stdout 兜底）：时间戳/等级/标签三段与日志模块输出完全同构
// （tag 固定 Yomk），位置两段为本通道固有增量；宏族按 YOMK_DIAG_<级别> 扩展
#define YOMK_DIAG_LOG_IMPL(levelTag, msg)                                                                                     \
    do                                                                                                                        \
    {                                                                                                                         \
        static_assert(yomkDiagTagWidth(levelTag) == 7, "diag level tag must be 7 chars wide to keep level columns aligned");  \
        std::cout << yomkLogLocalTimeFormatted() << " " << levelTag << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "]"      \
                  << " [" << __func__ << "] " << msg << std::endl;                                                            \
    } while (0)

#define YOMK_DIAG_TRACE(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_TRACE, msg)
#define YOMK_DIAG_DEBUG(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_DEBUG, msg)
#define YOMK_DIAG_INFO(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_INFO, msg)
#define YOMK_DIAG_WARN(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_WARN, msg)
#define YOMK_DIAG_ERROR(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_ERROR, msg)
#define YOMK_DIAG_FATAL(msg) YOMK_DIAG_LOG_IMPL(YOMK_DIAG_TAG_FATAL, msg)
