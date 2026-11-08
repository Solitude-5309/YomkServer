#pragma once

#include "yomk/log/YomkLoggerPayload.h"
#include "yomk/msg/YomkStdMsg.h"

// Logger 模块消息包（成员名均为 d）。
YOMK_DEFINE_MSG(yomk::payload::LogFile, LogFile, d);
YOMK_DEFINE_MSG(yomk::payload::Log, Log, d);
YOMK_DEFINE_MSG(yomk::payload::ConsoleLogProxy, ConsoleLogProxy, d);
YOMK_DEFINE_MSG(yomk::payload::FileLogProxy, FileLogProxy, d);
YOMK_DEFINE_MSG(yomk::payload::LoggerQuery, LoggerQuery, d);
YOMK_DEFINE_MSG(yomk::payload::LoggerInfo, LoggerInfo, d);
// Logger 控制台全局主闸态（log_mgr().console()/file().status() 返回数据：六级别主闸开关 + proxy）
YOMK_DEFINE_MSG(yomk::payload::LoggerState, LoggerState, d);
// Logger 全量内省（log_mgr().all() 返回数据；元素为单查同款 LoggerInfo，每条内嵌 LoggerState 状态段）
YOMK_DEFINE_MSG(std::vector<yomk::payload::LoggerInfo>, LoggerInfoList, d);
