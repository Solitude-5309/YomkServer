#pragma once

#include "YomkMsgs.h"
#include "YomkServerInfoPayload.h"
#include "YomkServerPayload.h"

// Server 核心域消息包（成员名均为 d）：核心载荷消息包，被服务请求/函数内省等共用。
// 标准标量/数组类型见 YomkMsgs.h；模块专属消息包见各 Yomk{Module}Msgs.h。
YOMK_DEFINE_MSG(yomk::payload::Response, Response, d);
YOMK_DEFINE_MSG(yomk::payload::FuncInfo, FuncInfo, d);
// FunctionPool / ServerInfo 全量函数内省（fn_pool().all() 与 srv_info().functions() 返回数据；
// 函数总数由 d.size() 承载）
YOMK_DEFINE_MSG(std::vector<yomk::payload::FuncInfo>, FuncInfoList, d);
