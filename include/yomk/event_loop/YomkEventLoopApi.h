#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "yomk/event_loop/YomkEventLoopMsgs.h"
#include "yomk/event_loop/YomkEventLoopPayload.h"
#include "yomk/event_loop/YomkEventLoopUrl.h"
#include "yomk/YomkStdMsgs.h"
#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerPayload.h"

// 事件循环域：单入口 yomk::event_loop()。循环名由 .loop(name) 分面承载，回调 / 消息名 /
// 事件数据 / tag / 采样条数一律前置累积，终结 op 全部无参——start / stop / destroy /
// post / post_wait 经请求域 .status() 三态收敛，list / get_info / all 返回 Response。
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
 * @li handle_fn(fn) 为单次临时处理器，优先于 start() 声明的默认处理函数
 *
 * @par 内省
 * @li yomk::event_loop().list()：全部循环名清单（StringArray，字典序）
 * @li yomk::event_loop().loop(name).tag_count(n).get_info()：指定循环状态与
 *     队列头 n 条 tag 采样（n 省略 = 缺省 3）
 * @li yomk::event_loop().all()：全部循环全量状态
 *
 * @warning builder 全程 auto 接收、仅限单表达式内联使用，禁止缓存复用！
 */
// 公开面仅暴露域入口, builder 机制内敛, 调用方全程以 auto 接收、从不具名引用类型
namespace yomk::detail
{
class EventLoopBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
/**
 * @brief 事件循环域入口：生命周期 / 投递 / 内省链式调用的起点
 * @return EventLoopBuilder（按值, 仅用于单表达式链式调用）
 */
inline detail::EventLoopBuilder event_loop();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段: builder ——
namespace yomk::detail
{
/**
 * @brief 事件循环域 builder：loop 分面定位 + 参数前置累积 + 无参终结
 */
class EventLoopBuilder
{
    std::string m_name;
    std::string m_msgName;             // msg_name() 累积, start 用（缺省 ""）
    YomkServiceFunc m_fn;              // handle_fn() 累积, start/post 共用（缺省 nullptr）
    YomkMsgPtr m_data;                 // msg() 累积, post/post_wait 用
    std::string m_tag;                 // tag() 累积, post/post_wait 用（缺省 ""）
    std::size_t m_tagCount = 3;        // tag_count() 累积, get_info 用（与 EventLoopQuery 缺省一致）
public:
    /**
     * @brief 定位目标循环（生命周期 / 投递 / 单查共用）
     * @param name 循环名
     * @return 自身链式引用
     */
    EventLoopBuilder& loop(std::string_view name)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_name = name;
        return *this;
    }
    /**
     * @brief 累积处理函数
     * @param fn start 时为循环默认服务函数; post 时为单次临时处理器（优先于默认函数）
     * @return 自身链式引用
     */
    EventLoopBuilder& handle_fn(YomkServiceFunc fn)
    {
        m_fn = std::move(fn);
        return *this;
    }
    /**
     * @brief 累积消息名（start 用，声明期望消息类型）
     * @param msgName 消息类型名, 可省 = ""
     * @return 自身链式引用
     */
    EventLoopBuilder& msg_name(std::string_view msgName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_msgName = msgName;
        return *this;
    }
    /**
     * @brief 累积事件数据（post/post_wait 用）
     * @param data 打包后的消息对象
     * @return 自身链式引用
     */
    EventLoopBuilder& msg(YomkMsgPtr data)
    {
        m_data = std::move(data);
        return *this;
    }
    /**
     * @brief 累积事件 tag（post/post_wait 用，随事件入队，供内省采样）
     * @param t tag 名, 可省 = ""
     * @return 自身链式引用
     */
    EventLoopBuilder& tag(std::string_view t)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_tag = t;
        return *this;
    }
    /**
     * @brief 累积队列头 tag 采样条数（get_info 用）
     * @param n 采样条数, 可省 = 3
     * @return 自身链式引用
     */
    EventLoopBuilder& tag_count(std::size_t n)
    {
        m_tagCount = n;
        return *this;
    }
    /**
     * @brief 启动指定事件循环终结（读 loop/handle_fn/msg_name 累积）
     * @return 三态 ResStatus; 未初始化返回 Invalid
     * @note 同名循环不重建：复用既有实例并触发其启动, 本次累积的 handle_fn/msg_name 不生效;
     *     仅新循环名会创建循环并绑定默认处理函数
     */
    yomk::payload::ResStatus start()
    {
        return yomk::server().url(yomk::url::loopStart).msg(yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{m_name, m_fn, m_msgName})).status();
    }
    /**
     * @brief 停止事件循环线程终结（保留未执行事件）
     * @return 三态 ResStatus
     */
    yomk::payload::ResStatus stop()
    {
        return yomk::server().url(yomk::url::loopStop).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief 异步投递事件终结（读 msg/loop/handle_fn/tag 累积）
     * @return 三态 ResStatus（仅表示入队结果，非执行结果）
     * @note 非阻塞
     */
    yomk::payload::ResStatus post()
    {
        return yomk::server().url(yomk::url::loopPost).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    /**
     * @brief 同步投递事件终结（读 msg/loop/handle_fn/tag 累积）
     * @return 三态 ResStatus
     * @note 阻塞等待事件执行完成
     */
    yomk::payload::ResStatus post_wait()
    {
        return yomk::server().url(yomk::url::loopPostWait).msg(yomk::msgs::Event::shared(yomk::payload::Event(m_name, m_data, m_fn, m_tag))).status();
    }
    /**
     * @brief 销毁事件循环并清空队列终结
     * @return 三态 ResStatus
     */
    yomk::payload::ResStatus destroy()
    {
        return yomk::server().url(yomk::url::loopDestroy).msg(yomk::msgs::String::shared(m_name)).status();
    }
    /**
     * @brief 查询全部循环名清单
     * @return Response 携带 StringArray（字典序）; 未初始化返回 Invalid
     */
    yomk::payload::Response list()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoops).request();
    }
    /**
     * @brief 查询指定循环状态与事件积压数
     * @return Response 携带循环状态 + 队列头 tag_count(n) 条 tag 采样; 未初始化返回 Invalid
     */
    yomk::payload::Response get_info()
    {
        if (!detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoop).msg(yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{m_name, m_tagCount})).request();
    }
    /**
     * @brief 查询全部循环全量状态
     * @return Response; 未初始化返回 Invalid
     */
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
