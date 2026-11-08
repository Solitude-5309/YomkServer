#pragma once

#include "YomkMsgs.h"
#include "YomkServerInfoPayload.h"

// ServerInfo 模块消息包（成员名均为 d）。
// 函数元信息内省（fn_pool().get_info()/all() 与 srv_info().get_info()/functions() 返回数据）
YOMK_DEFINE_MSG(yomk::payload::FuncInfo, FuncInfo, d);
// 全量函数内省清单（函数总数由 d.size() 承载）
YOMK_DEFINE_MSG(std::vector<yomk::payload::FuncInfo>, FuncInfoList, d);
// ServerInfo 全量拓扑内省（yomk::srv_info().all() 返回数据；层级为服务 → 函数元信息）
YOMK_DEFINE_MSG(std::vector<yomk::payload::ServerServiceInfo>, ServerServiceInfoList, d);
