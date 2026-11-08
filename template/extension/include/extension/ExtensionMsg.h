#pragma once

#include "ExtensionPayload.h"
#include "yomk/msg/YomkMsg.h"

// Extension 扩展专属消息包（成员名均为 d，与 SDK 各 Yomk{Module}Msg.h 同一设计）。
// 当前模板 /version 返回值复用标准类型 msgs::String，暂无专属消息；
// 新增业务消息在此用 YOMK_DEFINE_MSG 声明，参考 yomk/fn_pool/YomkFunctionPoolMsg.h：
//
// YOMK_DEFINE_MSG(yomk::payload::ExtTask, ExtTask, d);
