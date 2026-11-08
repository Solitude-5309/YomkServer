#pragma once

#include "YomkFunctionPoolPayload.h"
#include "YomkMsgs.h"

// FunctionPool 模块消息包（成员名均为 d）。
YOMK_DEFINE_MSG(yomk::payload::Function, Function, d);
YOMK_DEFINE_MSG(yomk::payload::CallFunction, CallFunction, d);
