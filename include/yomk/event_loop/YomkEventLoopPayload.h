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
 * @brief Event loop execution unit: enqueued on post, executed by handle() on the event loop thread
 */
// @zh: 事件循环执行单元：投递入队后在事件循环线程 handle() 执行
struct Event
{
    virtual ~Event() = default;
    std::string eventLoopName;
    YomkMsgPtr pkg;
    YomkServiceFunc serviceFunc;
    std::uint64_t eventId = 0;
    Response response;
    std::function<void()> waitCallback;
    std::string tag;
    Event() : eventLoopName(""), pkg(nullptr), serviceFunc(nullptr), waitCallback(nullptr), tag("") {}
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc)
        : eventLoopName(eventLoopName), pkg(pkg), serviceFunc(serviceFunc), waitCallback(nullptr), tag("")
    {
    }
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc, const std::string& tag)
        : eventLoopName(eventLoopName), pkg(pkg), serviceFunc(serviceFunc), waitCallback(nullptr), tag(tag)
    {
    }
    /** @brief Execution entry on the event loop thread, derived classes may override to customize behavior */
    // @zh: 事件循环线程执行入口，派生类可重写定制行为
    virtual void handle()
    {
        if (serviceFunc)
        {
            response = serviceFunc(pkg);
        }
    }
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
