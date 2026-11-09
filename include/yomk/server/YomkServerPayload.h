#pragma once
#include <functional>
#include <string>

#include "yomk/msg/YomkMsg.h"

// 核心载荷:请求/响应状态与跨模块通用类型.
// 函数元信息 FuncInfo 见 YomkServerInfoPayload.h;模块专属载荷见 YomkContextPayload.h /
// YomkLoggerPayload.h / YomkEventLoopPayload.h / YomkFunctionPoolPayload.h.
namespace yomk::payload
{

/**
 * @brief Three-state converged result of a request/operation
 * @li Invalid: invalid parameters or uninitialized (intercepted at framework level, never reached the server)
 * @li Succeeded: success
 * @li Failed: execution failed on the server side, including not-found (a missing resource uses this value; Invalid is only for invalid parameters)
 */
// @zh: 请求/操作三态收敛结果
// @zh: li Invalid：参数无效或未初始化（框架层拦截，未到服务端）
// @zh: li Succeeded：成功
// @zh: li Failed：服务端执行失败，含 not-found（资源不存在按本值，Invalid 仅用于参数无效）
enum class ResStatus
{
    Invalid = -1,
    Succeeded = 0,
    Failed = 1,
};

/**
 * @brief Unified response packet: status + text description + data carrier
 */
// @zh: 统一应答包：状态 + 文字说明 + 数据载体
struct Response
{
    Response() = default;
    Response(ResStatus status, const std::string& msg = "", YomkMsgPtr d = nullptr)
        : m_status(status), m_msg(msg), m_data(d)
    {
    }
    ResStatus m_status = ResStatus::Invalid;  // 本次请求的状态
    std::string m_msg;                        // 对状态的说明(不携带返回数据)
    YomkMsgPtr m_data = nullptr;              // 携带的返回数据
};

}  // namespace yomk::payload

/**
 * @brief Service function signature: receives the request message, returns a response
 * @note Runs on the request worker thread; should have no blocking side effects, the returned Response is the response posted back to the caller
 */
// @zh: 功能函数签名：接收请求消息，返回响应
// @zh: note 在请求工作线程执行；应无阻塞副作用，返回的 Response 即投递给调用方的响应
using YomkServiceFunc = std::function<yomk::payload::Response(YomkMsgPtr pkg)>;
/**
 * @brief Async request completion callback: receives the response after an async() request completes
 * @note Runs on the async request thread pool; must not synchronously wait for the same request inside the callback, does not throw
 */
// @zh: 异步请求完成回调：async() 请求完成后收到响应
// @zh: note 在异步请求线程池执行；回调内不得再同步等待该请求，不抛异常
using YomkResponseFunc = std::function<void(yomk::payload::Response response)>;
