#pragma once
#include <string>
#include <memory>
#include <type_traits>
#include <utility>
#include <iostream>
#include <map>

#include "YomkPkg.h"
#include "YomkDefine.h"

class YomkServer;
class YomkServicePrivate;
class YOMKSERVER_EXPORT YomkService : public std::enable_shared_from_this<YomkService>
{
public:
    YomkService(YomkServer *server);
    virtual ~YomkService() {}

public:
    // 设置服务名（建议以 / 开头，如 "/MyService"）：须在注册（addService）前调用，注册后改名将被拒绝
    void name(const std::string &name);
    std::string name();
    // 框架内部接口，用户代码不应调用
    void markRegistered();
    // 框架内部接口，用户代码不应调用；deleted() 可查询服务是否已被删除/同名替换（供判活）
    void markDeleted();
    bool deleted() const;

public:
    virtual int init() = 0;
    virtual void deinit() {}

public:
    // 弱绑定守卫（泛型模板），供外流回调（功能函数/FunctionPool/EventLoop/Context checker·monitor/异步响应）使用。
    // 服务被删除（YOMK_DEL_SERVICE/同名替换）后回调立即安全丢弃，无需等待引用归零：
    // YomkResponse 返回 eNo，Context checker 默认放行 eAccept，void 回调直接丢弃，其余返回默认值；
    // YOMK_SHUTDOWN 走排空语义，排空期回调照常执行。
    // 子类仍须在 deinit() 中停止非弱绑定路径的生产者（线程/定时器/外部注册）。
    template <typename Func>
    auto weakFunc(Func func)
    {
        std::weak_ptr<YomkService> weakSelf = weak_from_this();
        if (weakSelf.expired())
        {
            YOMK_ERR_POS_LOG("weakFunc called before service is owned by server (in constructor?), callback will never fire!");
        }
        return [weakSelf, func](auto &&...args) -> decltype(auto)
        {
            using Ret = decltype(func(args...));
            auto self = weakSelf.lock();
            if (!self || self->deleted())
            {
                YOMK_ERR_POS_LOG("service has been deleted or unregistered, callback ignored.");
                if constexpr (std::is_void_v<Ret>)
                    return;
                else if constexpr (std::is_same_v<Ret, YomkResponse>)
                    return YomkResponse{YomkResponse::eNo, "service has been deleted or unregistered, callback ignored."};
                else if constexpr (std::is_same_v<Ret, yomk::ContextChecker::ECheckStatus>)
                    return yomk::ContextChecker::eAccept;
                else
                    return Ret{};
            }
            return func(std::forward<decltype(args)>(args)...);
        };
    }
    void installFunc(const std::string &funcName, YomkServiceFunc func, const std::string &msgName = "");
    std::map<std::string, YomkFuncInfo> funcInfos();
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    std::shared_ptr<YomkServicePrivate> m_p;
};