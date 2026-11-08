#pragma once
#include <functional>
#include <string>

#include "YomkMsg.h"

// 核心载荷：请求/响应状态与跨模块通用类型。
// 函数元信息 FuncInfo 见 YomkServerInfoPayload.h；模块专属载荷见 YomkContextPayload.h /
// YomkLoggerPayload.h / YomkEventLoopPayload.h / YomkFunctionPoolPayload.h。
namespace yomk::payload
{

/**
 * @brief 请求/操作三态收敛结果
 * @li Invalid：参数无效或未初始化（框架层拦截, 未到服务端）
 * @li Succeeded：成功
 * @li Failed：服务端执行失败, 含 not-found（资源不存在按本值, Invalid 仅用于参数无效）
 */
enum class ResStatus
{
    Invalid = -1,
    Succeeded = 0,
    Failed = 1,
};

/**
 * @brief 统一应答包：状态 + 文字说明 + 数据载体
 */
struct Response
{
    Response() = default;
    Response(ResStatus status, const std::string& msg = "", YomkMsgPtr d = nullptr)
        : m_status(status), m_msg(msg), m_data(d)
    {
    }
    ResStatus m_status = ResStatus::Invalid;  // 本次请求的状态
    std::string m_msg;                        // 对状态的说明（不携带返回数据）
    YomkMsgPtr m_data = nullptr;              // 携带的返回数据
};

}  // namespace yomk::payload

using YomkServiceFunc = std::function<yomk::payload::Response(YomkMsgPtr pkg)>;
using YomkResponseFunc = std::function<void(yomk::payload::Response response)>;

namespace yomk::payload
{

// 通用弱绑定兜底返回与 Context 门控共用，故保留在核心载荷
enum class CheckStatus
{
    Accept,
    Reject
};

}  // namespace yomk::payload
