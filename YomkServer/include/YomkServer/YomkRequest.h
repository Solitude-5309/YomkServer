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
// 约束：builder 持 url 视图，仅限单表达式内联使用，不得缓存复用（缓存将悬垂）
namespace yomk
{
// 异步分面视图：async() 之后仅允许异步终结 request(pkg)
class AsyncRequestBuilder
{
    friend class RequestBuilder;
    // url 以 view 透传（请求链全程 string_view 化成果，builder 不产生拷贝）
    std::string_view m_url;
    YomkResponseFunc m_func;
    AsyncRequestBuilder(std::string_view url, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_url(url), m_func(std::move(func))
    {
    }

public:
    // 异步请求终结：投递后立即返回，结果经回调送达；未初始化记 diag 后丢弃请求
    void request(YomkMsgPtr pkg)
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return;
        }
        return srv->asyncRequest(m_url, pkg, m_func);
    }
};
// 请求域 builder：url 累积 + request/status 同步终结 + async 异步分面
class RequestBuilder
{
    std::string_view m_url;

public:
    // 累积目标 url（自定义服务传注册名/原始串；域 builder 内部取自 yomk::url 常量池）
    RequestBuilder& url(std::string_view u)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_url = u;
        return *this;
    }
    // 同步请求终结；未初始化返回 Response(Invalid, "YomkServer is not init") 并记 diag
    yomk::payload::Response request(YomkMsgPtr pkg)
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return srv->request(m_url, pkg);
    }
    // 三态收敛终结：失败原因经错误日志承载，返回值仅保留 ResStatus
    // （各域 builder 写路径终结 op 与自定义服务客户端共用）
    yomk::payload::ResStatus status(YomkMsgPtr pkg)
    {
        yomk::payload::Response resp = request(std::move(pkg));
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.m_msg.empty() ? "request failed" : resp.m_msg));
        }
        return resp.m_status;
    }
    // 异步分面：func 为结果回调（nullptr = 仅投递不取回）
    AsyncRequestBuilder async(YomkResponseFunc func = nullptr)
    {
        return AsyncRequestBuilder(m_url, std::move(func));
    }
};
// 请求域入口：向当前 server 发起请求（与生命周期取实例的 serverInstance 相区分）
inline RequestBuilder server()
{
    return RequestBuilder{};
}
namespace detail
{
// 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值（仅表面层终结 op 使用）
inline bool requireServer()
{
    if (YomkServerRef::serverSnapshot())
        return true;
    YOMK_DIAG_ERROR("YomkServer is not init");
    return false;
}
}
}
