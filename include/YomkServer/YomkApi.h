#pragma once
// YomkServer 伞头：纯聚合, 一次引入全部公开表面。
// 各域能力均由链式入口暴露: yomk::server() 及 server/log/context/eventloop/functionpool
// 域头（见 YomkServerApi.h / YomkLogApi.h / YomkContextApi.h / YomkEventLoopApi.h /
// YomkFunctionPoolApi.h）。对编译隔离敏感的工程可直接包含单个域头, 无需本伞头。
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
