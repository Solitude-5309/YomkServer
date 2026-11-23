#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "yomk/event_loop/YomkEventLoopPayload.h"
#include "yomk/server/YomkServerPayload.h"

/**
 * @brief Event loop internal execution unit: runtime state only (event id assigned and handler finalized under the enqueue lock)
 * @note onDone is the single result slot: async post carries EventOpts.onDone (may be empty); sync postWait rebinds it to an internal wait closure
 */
// @zh: 事件循环内部执行单元：仅运行时状态（eventId 与 handler 在入队锁内定稿）
// @zh: note onDone 是唯一结果出口槽：异步 post 搬运 EventOpts.onDone（可空）；同步 postWait 改绑内部等待闭包
struct Event
{
    std::uint64_t eventId = 0;  // 入队锁内赋号
    std::string loopName;
    YomkMsgPtr msg;
    YomkServiceFunc handler;    // 入队锁内定稿（已合并默认函数）
    std::string tag;
    YomkResponseFunc onDone;    // 完成回调：空 = 无人关心结果（默认异步）；同步 postWait 由框架注入内部闭包占用该槽
};
using EventPtr = std::shared_ptr<Event>;
