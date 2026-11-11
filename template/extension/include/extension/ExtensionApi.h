#pragma once
#include "ExtensionPayload.h"
#include "ExtensionUrl.h"
#include "yomk/server/YomkServerApi.h"

// Extension 模块 API 头（四件套之 Api，与 SDK 各 Yomk{Module}Api.h 同一链式域入口设计）。
// 设计约定：
// 1. Api 头是模块自己的 API，只写本模块业务的终结方法（模板仅 version()），
//    不重复封装通用请求链（url/msg/request 归 yomk::server()）；
// 2. 也可以完全不写 API 方法，但须在本文件注释写清统一调用方式及 url 的功能作用，
//    如：yomk::server().url(yomk::url::extVersion).request() —— extVersion 即 /ExtensionService/version 版本查询；
// 3. 请求一律优先使用统一请求接口 yomk::server().url(<url>).request()，
//    扩展终结方法只是其上的薄封装（如 yomk::ext().version()），屏蔽裸 URL、不另造请求通道。
namespace yomk::detail
{
class ExtBuilder
{
public:
    /**
     * @brief 版本查询终结
     * @return Response 应答包; 未初始化返回 Invalid
     */
    yomk::payload::Response version()
    {
        return yomk::server().url(yomk::url::extVersion).request();
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
