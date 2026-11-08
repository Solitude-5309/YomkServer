#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "YomkExport.h"
#include "YomkPayload.h"
#include "YomkDiag.h"

class YomkServer;
class YomkServerPrivate;
class YomkServicePrivate;

/**
 * @brief 自定义服务基类：派生类在 init() 内 installFunc() 注册功能函数, 即接入请求路由
 * @note 实例所有权注册后移交 YomkServer; 功能函数经 weakFunc 绑定, 服务注销后回调自动短路
 */
class YOMKSERVER_EXPORT YomkService : public std::enable_shared_from_this<YomkService>
{
    friend class YomkServerPrivate;

public:
    YomkService(YomkServer* server);
    virtual ~YomkService() {}

public:
    void name(const std::string& name);
    std::string name();
    /**
     * @brief 服务是否已被注销
     * @note 线程安全; 供回调内自检存活
     */
    bool isDeleted() const;

public:
    /**
     * @brief 服务初始化（注册进 server 时被调一次）
     * @return 0 成功; 非 0 注册失败并回滚
     * @pre 须在此函数体内 installFunc()（构造期 weak_from_this 无效, 回调永不触发）
     */
    virtual int init() = 0;
    /**
     * @brief 服务注销/关闭时的清理钩子, 缺省空实现
     */
    virtual void deinit() {}

protected:
    /**
     * @brief 注册成员函数为服务功能函数（自动 weakFunc 包装）
     * @param funcName 路由名（"/" 前缀, url = 服务名 + funcName）
     * @param func 成员函数指针, 形如 Response f(YomkMsgPtr)
     * @param msgName 期望消息类型名（仅内省展示, 可空）
     * @param description 函数说明（仅内省展示, 可空）
     */
    template <typename Class, typename Res, typename Arg>
    void installFunc(const std::string& funcName, Res (Class::*func)(Arg), const std::string& msgName = "",
                     const std::string& description = "")
    {
        installFunc(funcName, weakFunc(func), msgName, description);
    }
    std::map<std::string, yomk::payload::FuncInfo> funcInfos();
    yomk::payload::Response invoke(std::string_view funcName, YomkMsgPtr pkg = nullptr);
    yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg = nullptr);
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
                YOMK_DIAG_ERROR("service has been deleted or unregistered, callback ignored.");
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