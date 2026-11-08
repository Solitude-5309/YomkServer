#pragma once

#include "yomk/event_loop/YomkEventLoopPayload.h"
#include "yomk/YomkStdMsgs.h"

// EventLoop 模块消息包（成员名均为 d）。
YOMK_DEFINE_MSG(yomk::payload::Event, Event, d);
YOMK_DEFINE_MSG(yomk::payload::EventLoop, EventLoop, d);
YOMK_DEFINE_MSG(yomk::payload::EventLoopInfo, EventLoopInfo, d);
YOMK_DEFINE_MSG(yomk::payload::EventLoopQuery, EventLoopQuery, d);
// EventLoop 全量循环内省（yomk::event_loop().all() 返回数据；m_nextTags 取 3 条与单查缺省一致）
YOMK_DEFINE_MSG(std::vector<yomk::payload::EventLoopInfo>, EventLoopInfoList, d);
