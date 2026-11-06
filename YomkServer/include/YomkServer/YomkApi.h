#pragma once
// YomkServer 伞头：纯聚合（闭环 6，2026-11-06 决议：生命周期亦链式）。
// class YomkAPI 已整体退役——VERSION/BOOT/SERVER_INFO 12 个自由函数并入 yomk::server()
// 生命周期/服务管理/内身分面（见 YomkServerApi.h；delService 终结定名 remove，delete 为
// C++ 保留字）；REQ/LOG/CONTEXT/EVENTLOOP/FUNCTIONPOOL 各域此前已随闭环 1–5 迁出。
// 外部用户直接包含各域头即可获得编译隔离，本伞头一次引入全部公开表面。
#include "YomkBoot.h"
#include "YomkContextApi.h"
#include "YomkDiag.h"
#include "YomkEventLoopApi.h"
#include "YomkExport.h"
#include "YomkFunctionPoolApi.h"
#include "YomkLogApi.h"
#include "YomkMsgs.h"
#include "YomkPayload.h"
#include "YomkServerApi.h"
#include "YomkServer.h"
#include "YomkServerRef.h"
#include "YomkService.h"
#include "YomkUrl.h"
