#pragma once

#include "yomk/ctx/YomkContextPayload.h"
#include "yomk/msg/YomkStdMsg.h"

// Context 模块消息包(成员名均为 d).
YOMK_DEFINE_MSG(yomk::payload::Context, Context, d);
// Context 组寻址载体(/destroy 与 /key 请求报文:定位某组下的某键,组名缺省 main)
YOMK_DEFINE_MSG(yomk::payload::ContextGroupKey, ContextGroupKey, d);
YOMK_DEFINE_MSG(yomk::payload::ContextKeyInfo, ContextKeyInfo, d);
// Context 键元信息清单(ContextGroupInfo.m_keys 的元素类型)
YOMK_DEFINE_MSG(std::vector<yomk::payload::ContextKeyInfo>, ContextKeyInfoList, d);
// Context 组级内省条目(ContextGroupInfoList 元素;层级为组 → 键元信息)
YOMK_DEFINE_MSG(yomk::payload::ContextGroupInfo, ContextGroupInfo, d);
// Context 组级全量内省(yomk::ctx().all() 返回数据)
YOMK_DEFINE_MSG(std::vector<yomk::payload::ContextGroupInfo>, ContextGroupInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::ContextChecker, ContextChecker, d);
YOMK_DEFINE_MSG(yomk::payload::ContextMonitor, ContextMonitor, d);
