#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

/**
 * @brief Event post payload: user-supplied inputs only (loop name, message, one-shot handler, tag, optional completion callback)
 * @note Runtime state (event id, merged handler) lives in the internal execution unit of the event loop
 */
// @zh: 事件投递载荷：仅用户输入（循环名 / 消息 / 单次处理器 / tag / 可选完成回调）
// @zh: note 运行时状态（事件 id、已合并处理器）存放于事件循环内部执行单元
struct EventOpts
{
    std::string eventLoopName;    // 目标循环名
    YomkMsgPtr msg;               // 事件负载
    YomkServiceFunc serviceFunc;  // 单次处理器，空则回落循环默认处理函数
    std::string tag;              // 标签（内省采样用）
    YomkResponseFunc onDone;      // 异步完成回调，可空；仅 post 生效，与 post_wait 互斥
};

/** @brief Event loop creation payload (name + default service function) */
// @zh: 事件循环创建载荷（名称 + 默认处理函数）
struct EventLoop
{
    std::string eventLoopName;
    YomkServiceFunc defaultServiceFunc;
    std::string msgName;  // 默认处理函数期望的消息类型名(仅内省元数据,可为空)
};

/** @brief Event loop single-query payload (dedicated to /YomkEventLoop/loop): name plus queue-head tag sample count, typed fields without parsing ambiguity */
// @zh: 事件循环单查载荷（/YomkEventLoop/loop 专用）：名称 + 队列头 tag 采样数，类型化字段无解析歧义
struct EventLoopQuery
{
    std::string loopName;      // 循环名(按字面量精确匹配,不做任何切分)
    std::size_t tagCount = 3;  // 队列头采样条数(缺省 3,与全量内省缺省一致)
};

/** @brief Event loop introspection snapshot */
// @zh: 事件循环内省快照
struct EventLoopInfo
{
    std::string loopName;               // 循环名
    bool running = false;               // worker 是否运行中
    std::size_t pending = 0;            // 积压事件数
    bool hasDefaultFunc = false;        // 是否注册默认处理函数
    std::string msgName;                // 默认处理函数期望的消息类型名(可空)
    std::vector<std::string> nextTags;  // 队列头起最多 tagCount 条事件 tag(存原值,空串保留)
};

}  // namespace yomk::payload
