#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "ExtensionMsgs.h"
#include "ExtensionPayload.h"
#include "ExtensionUrl.h"
#include "yomk/server/YomkServerApi.h"

// Extension 模块 API 头（四件套之 Api，与 SDK 各 Yomk{Module}Api.h 同一链式域入口设计）：
// 前置累积（url/msg）+ 无参终结（request/version），屏蔽裸 URL 路由。
// builder 持 string_view 视图、按值传递，仅限单表达式内联使用，禁止缓存复用（悬垂引用）。
namespace yomk::detail
{
class ExtBuilder
{
    std::string_view m_url;
    YomkMsgPtr m_msg;

public:
    /**
     * @brief 累积目标 url（建议取自本头 yomk::url::ext* 常量）
     * @warning 仅持有视图不拷贝, 调用方须保证 u 在同表达式终结前有效
     */
    ExtBuilder& url(std::string_view u)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_url = u;
        return *this;
    }
    /**
     * @brief 累积请求消息, nullptr 为空消息
     */
    ExtBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    /**
     * @brief 同步请求终结
     * @return Response 应答包; 未初始化返回 Invalid
     */
    yomk::payload::Response request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return srv->request(m_url, m_msg);
    }
    /**
     * @brief 版本查询终结：等价 yomk::ext().url(yomk::url::extVersion).request()
     */
    yomk::payload::Response version()
    {
        return url(yomk::url::extVersion).request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief Extension 扩展域链式入口
 * @return ExtBuilder（按值, 仅用于单表达式链式调用）
 */
inline detail::ExtBuilder ext()
{
    return detail::ExtBuilder{};
}
}  // namespace yomk
