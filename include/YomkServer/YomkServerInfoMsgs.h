#pragma once

#include "YomkMsgs.h"
#include "YomkServerInfoPayload.h"

// ServerInfo 模块消息包（成员名均为 d）。
// ServerInfo 全量拓扑内省（yomk::server().all() 返回数据；层级为服务 → 函数元信息）
YOMK_DEFINE_MSG(std::vector<yomk::payload::ServerServiceInfo>, ServerServiceInfoList, d);
