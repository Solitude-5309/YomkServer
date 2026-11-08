#pragma once

#include "YomkMsgs.h"
#include "YomkServerPayload.h"

// Server 核心域消息包（成员名均为 d）：核心载荷消息包 Response，被服务请求通道共用。
// 标准标量/数组类型见 YomkMsgs.h；模块专属消息包见各 Yomk{Module}Msgs.h。
YOMK_DEFINE_MSG(yomk::payload::Response, Response, d);
