#pragma once

#include <iostream>

#include "YomkLogTime.h"

// 框架自诊断通道（logger 不可用时的裸 stdout 兜底）：时间戳/等级/标签三段与日志模块输出完全同构
// （tag 固定 Yomk），位置两段为本通道固有增量
#define YOMK_DIAG_ERR(msg)                                                                                                    \
    do                                                                                                                        \
    {                                                                                                                         \
        std::cout << yomkLogLocalTimeFormatted() << " [Error] [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__   \
                  << "] " << msg << std::endl;                                                                                \
    } while (0)
