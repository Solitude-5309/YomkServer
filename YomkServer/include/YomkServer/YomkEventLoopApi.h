#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "YomkMsgs.h"
#include "YomkPayload.h"
#include "YomkRequest.h"
#include "YomkUrl.h"

// 事件循环域：单入口 yomk::event_loop()。循环名由 .loop(name) 分面承载，回调 / 消息名 /
// 事件数据 / tag / 采样条数一律前置累积（约定 5），终结 op 全部无参——start / stop /
// destroy / post / post_wait 经请求域 .status() 三态收敛，list / get_info / all 返回
// Response。原 9 个 EVENTLOOP_API 垫片与 getEventLoopInfo 双重载随本域落地退役
// （双重载合并为 tag_count 累积，省略 = 缺省 3）。
/**
 * @brief 事件循环链式 Builder：生命周期 / 投递 / 内省
 *
 * @par 生命周期
 * yomk::event_loop().loop(name).handle_fn(fn).msg_name(msgName).start()
 * @li loop(name) 分面定位循环；handle_fn / msg_name 省略 = 缺省（nullptr / ""）
 * @li start() 无参终结，返回三态 ResStatus；未初始化返回 Invalid
 * @li yomk::event_loop().loop(name).stop()：停止循环线程（保留未执行事件）
 * @li yomk::event_loop().loop(name).destroy()：销毁循环并清空队列
 *
 * @par 事件投递
 * yomk::event_loop().msg(data).loop(name).handle_fn(fn).tag(tag).post()
 * @li msg(data) 承载事件数据；handle_fn / tag 省略 = 缺省（nullptr / ""）
 * @li post() 异步投递；post_wait() 同步投递（阻塞等待执行完成）
 *
 * @par 内省
 * @li yomk::event_loop().list()：全部循环名清单（StringArray，字典序）
 * @li yomk::event_loop().loop(name).tag_count(n).get_info()：指定循环状态与
 *     队列头 n 条 tag 采样（n 省略 = 缺省 3；原单参/双参双重载合并于此）
 * @li yomk::event_loop().all()：全部循环全量状态
 *
 * @warning builder 全程 auto 接收、仅限单表达式内联使用，禁止缓存复用！
 */
// 公开面：仅域入口（约定 8：入口暴露、机制内敛）
namespace yomk::detail
{
class EventLoopBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
// 事件循环域入口：生命周期 / 投递 / 内省
inline detail::EventLoopBuilder event_loop();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段：builder（用户无需感知）——
namespace yomk::detail
{
class EventLoopBuilder
{
    std::string m_name;
    std::string m_msgName;             // msg_name() 累积，start 用（缺省 ""）
    YomkServiceFunc m_fn;              // handle_fn() 累积，start/post 共用（缺省 nullptr）
    YomkMsgPtr m_data;                 // msg() 累积，post/post_wait 用
    std::string m_tag;                 // tag() 累积，post/post_wait 用（缺省 ""）
    std::size_t m_tagCount = 3;        // tag_count() 累积，get_info 用（与 EventLoopQuery 缺省一致）
public:
    // 循环名分面（生命周期 / 投递 / 单查共用）
    EventLoopBuilder& loop(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    // 累积处理函数（start 缺省服务函数 / post 单次处理器）
    EventLoopBuilder& handle_fn(YomkServiceFunc fn)
    {
        m_fn = std::move(fn);
        return *this;
    }
    // 累积消息名（start 用，声明期望消息类型）
    EventLoopBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    // 累积事件数据（post/post_wait 用）
    EventLoopBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    // 累积事件 tag（post/post_wait 用，可省 = ""）
    EventLoopBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    // 累积队列头 tag 采样条数（get_info 用，可省 = 3）
    EventLoopBuilder& tag_count(std::size_t n)
    {
        m_tagCount = n;
        return *this;
    }
    // 启动指定事件循环（无参动作，读 loop/handle_fn/msg_name 累积）
    yomk::payload::ResStatus start()
    {
        return yomk::server().url(yomk::url::loopStart).msg(yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{m_name, m_fn, m_msgName})).status();
    }
    // 停止事件循环线程（保留未执行事件）
    yomk::payload::ResStatus stop()
    {
        return yomk::server().url(yomk::url::loopStop).msg(yomk::msgs::String::shared(m_name)).status();
    }
    // 异步投递事件（无参动作，读 msg/loop/handle_fn/tag 累积）
    yomk::payload::ResStatus post()
    {
        return yomk::server().url(yomk::url::loopPost).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    // 同步投递事件（阻塞等待执行完成）
    yomk::payload::ResStatus post_wait()
    {
        return yomk::server().url(yomk::url::loopPostWait).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    // 销毁事件循环并清空队列
    yomk::payload::ResStatus destroy()
    {
        return yomk::server().url(yomk::url::loopDestroy).msg(yomk::msgs::String::shared(m_name)).status();
    }
    // 全部循环名清单（StringArray，字典序）
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoops).request();
    }
    // 指定循环状态与事件积压数（tag_count 省略 = 缺省 3，双重载合并后单路径）
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoop).msg(yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{m_name, m_tagCount})).request();
    }
    // 全部循环全量状态
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopAll).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
inline detail::EventLoopBuilder event_loop()
{
    return detail::EventLoopBuilder{};
}
}  // namespace yomk
