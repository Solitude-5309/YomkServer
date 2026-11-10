#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "yomk/ctx/YomkContextPayload.h"  // CheckStatus:weakFunc checker 回调兜底返回
#include "yomk/YomkDiag.h"
#include "yomk/YomkExport.h"
#include "yomk/srv_info/YomkServerInfoPayload.h"
#include "yomk/server/YomkServerPayload.h"

class YomkServer;
class YomkServerPrivate;
class YomkServicePrivate;

/**
 * @brief Custom service base class: derived classes register function members via installFunc() inside init(), which hooks them into request routing
 * @note Instance ownership is transferred to YomkServer after registration; functions are bound via weakFunc, callbacks short-circuit automatically after the service is unregistered
 */
// @zh: 自定义服务基类：派生类在 init() 内 installFunc() 注册功能函数，即接入请求路由
// @zh: note 实例所有权注册后移交 YomkServer；功能函数经 weakFunc 绑定，服务注销后回调自动短路
class YOMK_EXPORT YomkService : public std::enable_shared_from_this<YomkService>
{
    friend class YomkServerPrivate;

public:
    YomkService(YomkServer* server);
    virtual ~YomkService() {}

public:
    /** @brief Set the service name (must be done before registering into the table; locked by the framework afterwards) */
    // @zh: 设置服务名（须在注册入表前；入表后由框架锁定）
    void name(const std::string& name);
    /** @brief Service name */
    // @zh: 服务名
    std::string name();
    /**
     * @brief Whether the service has been unregistered
     * @note Thread-safe; for liveness self-check inside callbacks
     */
    // @zh: 服务是否已被注销
    // @zh: note 线程安全；供回调内自检存活
    bool isDeleted() const;

public:
    /**
     * @brief Service initialization (called once when registered into the server)
     * @return 0 on success; non-zero fails the registration and rolls back
     * @pre installFunc() must be called inside this function body (weak_from_this is invalid during construction, callbacks would never fire)
     */
    // @zh: 服务初始化（注册进 server 时被调一次）
    // @zh: return 0 成功；非 0 注册失败并回滚
    // @zh: pre 须在此函数体内 installFunc()（构造期 weak_from_this 无效，回调永不触发）
    virtual int init() = 0;
    /**
     * @brief Cleanup hook on service unregister/shutdown, default empty implementation
     */
    // @zh: 服务注销/关闭时的清理钩子，缺省空实现
    virtual void deinit() {}

protected:
    /**
     * @brief Register a member function as a service function (wrapped by weakFunc automatically)
     * @param funcName Routing name ("/" prefix, url = service name + funcName)
     * @param func Member function pointer, of the form Response f(YomkMsgPtr)
     * @param msgName Expected message type name (for introspection display only, may be empty)
     * @param description Function description (for introspection display only, may be empty)
     */
    // @zh: 注册成员函数为服务功能函数（自动 weakFunc 包装）
    // @zh: funcName 路由名（"/" 前缀，url = 服务名 + funcName）
    // @zh: func 成员函数指针，形如 Response f(YomkMsgPtr)
    // @zh: msgName 期望消息类型名（仅内省展示，可空）
    // @zh: description 函数说明（仅内省展示，可空）
    template <typename Class, typename Res, typename Arg>
    void installFunc(const std::string& funcName, Res (Class::*func)(Arg), const std::string& msgName = "",
                     const std::string& description = "")
    {
        installFunc(funcName, weakFunc(func), msgName, description);
    }
    /** @brief Metadata of the functions already registered by this service */
    // @zh: 本服务已注册的功能函数元信息
    std::map<std::string, yomk::payload::FuncInfo> funcInfos();
    /** @brief Direct call within the service: bypasses URL routing, semantics identical to request() */
    // @zh: 服务内直调：不经 URL 路由，语义同 request()
    yomk::payload::Response invoke(std::string_view funcName, YomkMsgPtr pkg = nullptr);
    /** @brief Synchronous request routed via the server (entry for inter-service communication); blocks until the reply */
    // @zh: 经服务器路由的同步请求（服务间通信入口）；阻塞至应答
    yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg = nullptr);
    /** @brief Asynchronous request routed via the server; callback on completion (may be empty = post only) */
    // @zh: 经服务器路由的异步请求；完成后回调（可为空=仅投递）
    void asyncRequest(std::string_view url, YomkMsgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
    template <typename Class, typename Ret, typename Arg>
    auto weakFunc(Ret (Class::*func)(Arg))
    {
        Class* self = static_cast<Class*>(this);
        return weakFunc([self, func](Arg pkg) -> Ret { return (self->*func)(std::forward<Arg>(pkg)); });
    }
    void installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
                     const std::string& description = "");
    template <typename Func>
    auto weakFunc(Func func)
    {
        std::weak_ptr<YomkService> weakSelf = weak_from_this();
        if (weakSelf.expired())
        {
            YOMK_DIAG_ERROR(
                "weakFunc called before service is owned by server (in constructor?), callback will never fire!");
        }
        return [weakSelf, func](auto&&... args) -> decltype(auto)
        {
            using Ret = decltype(func(args...));
            auto self = weakSelf.lock();
            if (!self || self->isDeleted())
            {
                YOMK_DIAG_ERROR("service has been deleted or unregistered, callback ignored");
                if constexpr (std::is_void_v<Ret>)
                    return;
                else if constexpr (std::is_same_v<Ret, yomk::payload::Response>)
                    return yomk::payload::Response{
                        yomk::payload::ResStatus::Failed, "service has been deleted or unregistered, callback ignored."};
                else if constexpr (std::is_same_v<Ret, yomk::payload::CheckStatus>)
                    return yomk::payload::CheckStatus::Accept;
                else
                    return Ret{};
            }
            return func(std::forward<decltype(args)>(args)...);
        };
    }
private:
    std::shared_ptr<YomkServicePrivate> m_p;
};