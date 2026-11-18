#pragma once

#include "ExtensionPayload.h"
#include "yomk/msg/YomkMsg.h"

// Extension 扩展专属消息包（成员名均为 d，与 SDK 各 Yomk{Module}Msg.h 同一设计）。
// 消息经 YOMK_DEFINE_MSG_NS 声明：类落 yomk::msgs::ext 子命名空间（不占 yomk::msgs 根），
// kName 为全限定 "yomk::msgs::ext::ExtTask"，命名空间隔离与运行期类型 ID 唯一由同一机制保证；
// 新增业务消息在此按同形式追加，参考 yomk/fn_pool/YomkFunctionPoolMsg.h：
//
YOMK_DEFINE_MSG_NS(ext, yomk::payload::ext::ExtTask, ExtTask, d);
