#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "yomk/event_loop/YomkEventLoopMsg.h"
#include "yomk/event_loop/YomkEventLoopPayload.h"
#include "yomk/event_loop/YomkEventLoopUrl.h"
#include "yomk/msg/YomkStdMsg.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerPayload.h"

/**
 * @brief Event loop chained builder: lifecycle, post and introspection
 *
 * @par Lifecycle
 * yomk::event_loop().loop(name).handle_fn(fn).msg_name(msgName).start()
 * @li loop(name) facet locates the loop; omitting handle_fn / msg_name means default (nullptr / "")
 * @li start() terminates the chain with no argument and returns a tri-state ResStatus; returns Invalid if uninitialized
 * @li yomk::event_loop().loop(name).stop(): stops the loop thread (pending events are kept)
 * @li yomk::event_loop().loop(name).destroy(): destroys the loop and drains the queue
 *
 * @par Event post
 * yomk::event_loop().msg(data).loop(name).handle_fn(fn).tag(tag).post()
 * @li msg(data) carries the event data; omitting handle_fn / tag means default (nullptr / "")
 * @li post() posts asynchronously; post_wait() posts synchronously (blocks until the event finishes executing)
 * @li handle_fn(fn) is a one-shot temporary handler that takes precedence over the default function declared in start()
 *
 * @par Introspection
 * @li yomk::event_loop().list(): list of all loop names (StringArray, lexicographic order)
 * @li yomk::event_loop().loop(name).tag_count(n).get_info(): status of the given loop plus a sample of the first n tags at the queue head (n omitted means default 3)
 * @li yomk::event_loop().all(): full status of all loops
 *
 * @warning Receive the builder with auto throughout, use it only inline within a single expression, never cache and reuse it!
 */
// @zh: 事件循环链式 Builder：生命周期 / 投递 / 内省
// @zh: par 生命周期
// @zh: li loop(name) 分面定位循环；handle_fn / msg_name 省略 = 缺省（nullptr / ""）
// @zh: li start() 无参终结，返回三态 ResStatus；未初始化返回 Invalid
// @zh: li yomk::event_loop().loop(name).stop()：停止循环线程（保留未执行事件）
// @zh: li yomk::event_loop().loop(name).destroy()：销毁循环并排空队列
// @zh: par 事件投递
// @zh: li msg(data) 承载事件数据；handle_fn / tag 省略 = 缺省（nullptr / ""）
// @zh: li post() 异步投递；post_wait() 同步投递（阻塞等待执行完成）
// @zh: li handle_fn(fn) 为单次临时处理器，优先于 start() 声明的默认处理函数
// @zh: par 内省
// @zh: li yomk::event_loop().list()：全部循环名清单（StringArray，字典序）
// @zh: li yomk::event_loop().loop(name).tag_count(n).get_info()：指定循环状态与队列头 n 条 tag 采样（n 省略 = 缺省 3）
// @zh: li yomk::event_loop().all()：全部循环全量状态
// @zh: warning builder 全程 auto 接收，仅限单表达式内联使用，禁止缓存复用！
// 公开面仅暴露域入口, builder 机制内敛, 调用方全程以 auto 接收,从不具名引用类型
namespace yomk::detail
{
class EventLoopBuilder;  // 前置声明,定义见下方机制段
}
namespace yomk
{
/**
 * @brief Event loop domain entry: starting point for lifecycle, post and introspection chains
 * @return EventLoopBuilder (by value, for single-expression chained calls only)
 * @note Thread-safe, non-blocking
 */
// @zh: 事件循环域入口：生命周期 / 投递 / 内省链式调用的起点
// @zh: return EventLoopBuilder（按值，仅用于单表达式链式调用）
// @zh: note 线程安全，非阻塞
inline detail::EventLoopBuilder event_loop();  // 按值返回需完整类型,定义在机制段之后
}

// —— 机制段: builder ——
namespace yomk::detail
{
/**
 * @brief Event loop domain builder: loop facet targeting, upfront parameter accumulation and no-argument chain terminators
 */
// @zh: 事件循环域 builder：loop 分面定位 + 参数前置累积 + 无参终结
class EventLoopBuilder
{
    std::string m_name;
    std::string m_msgName;             // msg_name() 累积, start 用(缺省 "")
    YomkServiceFunc m_fn;              // handle_fn() 累积, start/post 共用(缺省 nullptr)
    YomkMsgPtr m_data;                 // msg() 累积, post/post_wait 用
    std::string m_tag;                 // tag() 累积, post/post_wait 用(缺省 "")
    std::size_t m_tagCount = 3;        // tag_count() 累积, get_info 用(与 EventLoopQuery 缺省一致)
public:
    /**
     * @brief Target the loop (shared by lifecycle, post and single query)
     * @param name Loop name
     * @return Reference to self for chaining
     */
    // @zh: 定位目标循环（生命周期 / 投递 / 单查共用）
    // @zh: name 循环名
    // @zh: return 自身链式引用
    EventLoopBuilder& loop(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    /**
     * @brief Accumulate the handler function
     * @param fn With start(), the loop default service function; with post(), a one-shot temporary handler (takes precedence over the default function)
     * @return Reference to self for chaining
     */
    // @zh: 累积处理函数
    // @zh: fn start 时为循环默认服务函数；post 时为单次临时处理器（优先于默认函数）
    // @zh: return 自身链式引用
    EventLoopBuilder& handle_fn(YomkServiceFunc fn)
    {
        m_fn = std::move(fn);
        return *this;
    }
    /**
     * @brief Accumulate the message name (used by start, declares the expected message type)
     * @param msgName Message type name, optional = ""
     * @return Reference to self for chaining
     */
    // @zh: 累积消息名（start 用，声明期望消息类型）
    // @zh: msgName 消息类型名，可省 = ""
    // @zh: return 自身链式引用
    EventLoopBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    /**
     * @brief Accumulate the event data (used by post / post_wait)
     * @param data Packed message object
     * @return Reference to self for chaining
     */
    // @zh: 累积事件数据（post / post_wait 用）
    // @zh: data 打包后的消息对象
    // @zh: return 自身链式引用
    EventLoopBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    /**
     * @brief Accumulate the event tag (used by post / post_wait, enqueued with the event, sampled by introspection)
     * @param t Tag name, optional = ""
     * @return Reference to self for chaining
     */
    // @zh: 累积事件 tag（post / post_wait 用，随事件入队，供内省采样）
    // @zh: t tag 名，可省 = ""
    // @zh: return 自身链式引用
    EventLoopBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    /**
     * @brief Accumulate the queue-head tag sample count (used by get_info)
     * @param n Sample count, optional = 3
     * @return Reference to self for chaining
     */
    // @zh: 累积队列头 tag 采样条数（get_info 用）
    // @zh: n 采样条数，可省 = 3
    // @zh: return 自身链式引用
    EventLoopBuilder& tag_count(std::size_t n)
    {
        m_tagCount = n;
        return *this;
    }
    /**
     * @brief Terminate the chain by starting the given event loop (reads loop / handle_fn / msg_name accumulations)
     * @return Tri-state ResStatus; returns Invalid if uninitialized
     * @note A loop with the same name is not rebuilt: the existing instance is reused and started, and the handle_fn / msg_name accumulated this time take no effect; only a new loop name creates a loop and binds the default service function
     */
    // @zh: 启动指定事件循环终结（读 loop / handle_fn / msg_name 累积）
    // @zh: return 三态 ResStatus；未初始化返回 Invalid
    // @zh: note 同名循环不重建：复用既有实例并触发其启动，本次累积的 handle_fn / msg_name 不生效；仅新循环名会创建循环并绑定默认处理函数
    yomk::payload::ResStatus start()
    {
        return yomk::server().url(yomk::url::loopStart).msg(yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{m_name, m_fn, m_msgName})).status();
    }
    /**
     * @brief Terminate the chain by stopping the event loop thread (pending events are kept)
     * @return Tri-state ResStatus
     */
    // @zh: 停止事件循环线程终结（保留未执行事件）
    // @zh: return 三态 ResStatus
    yomk::payload::ResStatus stop()
    {
        return yomk::server().url(yomk::url::loopStop).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief Terminate the chain by posting the event asynchronously (reads msg / loop / handle_fn / tag accumulations)
     * @return Tri-state ResStatus (only reflects the enqueue result, not the execution result)
     * @note Non-blocking
     */
    // @zh: 异步投递事件终结（读 msg / loop / handle_fn / tag 累积）
    // @zh: return 三态 ResStatus（仅表示入队结果，非执行结果）
    // @zh: note 非阻塞
    yomk::payload::ResStatus post()
    {
        return yomk::server().url(yomk::url::loopPost).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    /**
     * @brief Terminate the chain by posting the event synchronously (reads msg / loop / handle_fn / tag accumulations)
     * @return Tri-state ResStatus
     * @note Blocks until the event finishes executing
     */
    // @zh: 同步投递事件终结（读 msg / loop / handle_fn / tag 累积）
    // @zh: return 三态 ResStatus
    // @zh: note 阻塞等待事件执行完成
    yomk::payload::ResStatus post_wait()
    {
        return yomk::server().url(yomk::url::loopPostWait).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    /**
     * @brief Terminate the chain by destroying the event loop and draining the queue
     * @return Tri-state ResStatus
     */
    // @zh: 销毁事件循环并排空队列终结
    // @zh: return 三态 ResStatus
    yomk::payload::ResStatus destroy()
    {
        return yomk::server().url(yomk::url::loopDestroy).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief Query the list of all loop names
     * @return Response carrying a StringArray (lexicographic order); returns Invalid if uninitialized
     */
    // @zh: 查询全部循环名清单
    // @zh: return Response 携带 StringArray（字典序）；未初始化返回 Invalid
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loopLoops).request();
    }
    /**
     * @brief Query the status and pending event count of the given loop
     * @return Response carrying the loop status plus tag_count(n) tag samples from the queue head; returns Invalid if uninitialized
     */
    // @zh: 查询指定循环状态与事件积压数
    // @zh: return Response 携带循环状态 + 队列头 tag_count(n) 条 tag 采样；未初始化返回 Invalid
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loopLoop).msg(yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{m_name, m_tagCount})).request();
    }
    /**
     * @brief Query the full status of all loops
     * @return Response; returns Invalid if uninitialized
     */
    // @zh: 查询全部循环全量状态
    // @zh: return Response；未初始化返回 Invalid
    yomk::payload::Response all()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::loopAll).request();
    }
    /**
     * @brief Get the event_loop module version string
     * @return Version string (compile-time constant, does not go through the request channel)
     */
    // @zh: 获取 event_loop 模块版本号
    // @zh: return 版本字符串（编译期常量，不经请求通道）
    std::string version()
    {
        return YOMK_EVENT_LOOP_VERSION;
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
