#pragma once
#include <memory>
#include <string_view>
#include <utility>

#include "YomkDiag.h"
#include "YomkMsg.h"
#include "YomkPayload.h"
#include "YomkServerRef.h"

// 请求域：request 是刻意保留的基础接口——API 壳屏蔽 URL，但不垄断请求通道，
// 基于 YomkServer 写自定义服务直接依赖本头，链式 API 各 builder 终结 op 亦经此域。
// 链式形态：yomk::server() 域入口 → url() 累积 → request()/status() 同步终结，
// async() 分面（可带回调）→ request() 异步终结。
// 无状态设计：全部经 YomkServerRef 快照取当前 server，未初始化处理内聚在本头一层；
// 依赖单向（Diag/Msg/Payload/ServerRef），不依赖 YomkApi.h 与任何域头，可独立包含。

/**
 * @brief 服务请求链式 Builder，发起同步/异步服务调用
 *
 * @par 同步请求
 * yomk::server().url(url).msg(msg).request()
 * @li 返回 Response 对象
 * @li .msg(msg) 设置请求消息，YOMK_DEFINE_MSG 消息对象；nullptr 为空消息
 * @li 未初始化时返回 Invalid，并记录诊断信息
 *
 * @par 异步请求
 * yomk::server().url(url).async(func).msg(msg).request()
 * @li 投递消息后立即返回，结果通过回调 func 回调
 * @li func 可为 nullptr：仅投递，不取回应答
 * @li .msg(msg) 可放在 .async() 之前或之后
 *
 * @par 其他接口
 * @li .status()：终结接口，三态收敛，返回 ResStatus
 * @li url：支持服务注册名 / 原始字符串，取自 yomk::url 常量池
 *
 * @warning Builder 持 url 视图，仅限单表达式内联使用，禁止缓存复用，否则悬垂引用！
 */
// 公开面：仅域入口（约定 8：入口暴露、机制内敛，builder 类调用方全程 auto 接收、从不具名）
namespace yomk::detail
{
class RequestBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
// 请求域入口：向当前 server 发起请求（与生命周期取实例的 serverInstance 相区分）
inline detail::RequestBuilder server();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段：builder 与守卫（用户无需感知）——
namespace yomk::detail
{
// 异步分面视图：async() 之后允许 msg() 累积与异步终结 request()
class AsyncRequestBuilder
{
    friend class RequestBuilder;
    // url 以 view 透传（请求链全程 string_view 化成果，builder 不产生拷贝）
    std::string_view m_url;
    YomkMsgPtr m_msg;
    YomkResponseFunc m_func;
    AsyncRequestBuilder(std::string_view url, YomkMsgPtr msg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_url(url), m_msg(std::move(msg)), m_func(std::move(func))
    {
    }

public:
    // 累积请求消息（紧邻终结；省略 = nullptr 空消息）
    AsyncRequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    // 异步请求终结（无参动作）：投递后立即返回，结果经回调送达；未初始化记 diag 后丢弃请求
    void request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return;
        }
        return srv->asyncRequest(m_url, m_msg, m_func);
    }
};
// 请求域 builder：url/msg 累积 + request/status 无参同步终结 + async 异步分面
class RequestBuilder
{
    std::string_view m_url;
    YomkMsgPtr m_msg;

public:
    // 累积目标 url（自定义服务传注册名/原始串；域 builder 内部取自 yomk::url 常量池）
    RequestBuilder& url(std::string_view u)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_url = u;
        return *this;
    }
    // 累积请求消息（Fluent 约定：参数全部前置累积，终结 op 为无参动作；紧邻终结，省略 = nullptr 空消息）。
    // 命名 msg：实参是打包后的消息对象（YOMK_DEFINE_MSG 生成的 YomkMsg 派生类指针），非裸 payload 结构体
    RequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    // 同步请求终结（无参动作）；未初始化返回 Response(Invalid, "YomkServer is not init") 并记 diag
    yomk::payload::Response request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return srv->request(m_url, m_msg);
    }
    // 三态收敛终结（无参动作）：失败原因经错误日志承载，返回值仅保留 ResStatus
    // （各域 builder 写路径终结 op 与自定义服务客户端共用）
    yomk::payload::ResStatus status()
    {
        yomk::payload::Response resp = request();
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.m_msg.empty() ? "request failed" : resp.m_msg));
        }
        return resp.m_status;
    }
    // 异步分面：func 为结果回调（nullptr = 仅投递不取回）；已累积的消息携带进分面
    AsyncRequestBuilder async(YomkResponseFunc func = nullptr)
    {
        return AsyncRequestBuilder(m_url, std::move(m_msg), std::move(func));
    }
};
// 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值（仅表面层终结 op 使用）
inline bool requireServer()
{
    if (YomkServerRef::serverSnapshot())
        return true;
    YOMK_DIAG_ERROR("YomkServer is not init");
    return false;
}
}  // namespace yomk::detail

namespace yomk
{
inline detail::RequestBuilder server()
{
    return detail::RequestBuilder{};
}
}  // namespace yomk
