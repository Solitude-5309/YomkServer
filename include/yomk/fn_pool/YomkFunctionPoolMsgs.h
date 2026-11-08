#pragma once

#include "yomk/fn_pool/YomkFunctionPoolPayload.h"
#include "yomk/YomkStdMsgs.h"

// FunctionPool 模块消息包（成员名均为 d）。
YOMK_DEFINE_MSG(yomk::payload::Function, Function, d);
YOMK_DEFINE_MSG(yomk::payload::CallFunction, CallFunction, d);
