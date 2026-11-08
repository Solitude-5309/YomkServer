#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "YomkPayload.h"

namespace yomk::payload
{

struct Event
{
    virtual ~Event() = default;
    std::string m_eventLoopName;
    YomkMsgPtr m_pkg;
    YomkServiceFunc m_serviceFunc;
    std::uint64_t m_eventId = 0;
    Response m_response;
    std::function<void()> m_waitCallback;
    std::string m_tag;
    Event() : m_eventLoopName(""), m_pkg(nullptr), m_serviceFunc(nullptr), m_waitCallback(nullptr), m_tag("") {}
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc)
        : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag("")
    {
    }
    Event(const std::string& eventLoopName, YomkMsgPtr pkg, YomkServiceFunc serviceFunc, const std::string& tag)
        : m_eventLoopName(eventLoopName), m_pkg(pkg), m_serviceFunc(serviceFunc), m_waitCallback(nullptr), m_tag(tag)
    {
    }
    virtual void handle()
    {
        if (m_serviceFunc)
        {
            m_response = m_serviceFunc(m_pkg);
        }
    }
};

struct EventLoop
{
    std::string m_eventLoopName;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_msgName;  // 默认处理函数期望的消息类型名（仅内省元数据，可为空）
};

// 事件循环单查载荷（/YomkEventLoop/loop 专用）：名称 + 队列头 tag 采样数, 类型化字段无解析歧义
struct EventLoopQuery
{
    std::string m_loopName;      // 循环名（按字面量精确匹配，不做任何切分）
    std::size_t m_tagCount = 3;  // 队列头采样条数（缺省 3，与全量内省缺省一致）
};

struct EventLoopInfo
{
    std::string m_loopName;               // 循环名
    bool m_running = false;               // worker 是否运行中
    std::size_t m_pending = 0;            // 积压事件数
    bool m_hasDefaultFunc = false;        // 是否注册默认处理函数
    std::string m_msgName;                // 默认处理函数期望的消息类型名（可空）
    std::vector<std::string> m_nextTags;  // 队列头起最多 tagCount 条事件 tag（存原值，空串保留）
};

}  // namespace yomk::payload
