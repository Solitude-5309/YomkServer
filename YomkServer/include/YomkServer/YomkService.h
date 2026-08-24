#pragma once
#include <string>
#include <memory>
#include <type_traits>
#include <utility>
#include <iostream>

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
    void name(const std::string &name);
    std::string name();

public:
    virtual int init() = 0;
    virtual void deinit() {}

public:
    // 弱绑定守卫（泛型模板）：回调触发时先 lock() 判活，服务已删除则安全丢弃，
    // 供外流回调（功能函数/FunctionPool/EventLoop/Context checker·monitor/异步响应）使用。
    // 返回的泛型 lambda 按调用处目标 std::function 类型隐式转换，一个模板覆盖全部回调签名：
    // YomkResponse 返回 eNo，Context checker 默认放行 eAccept，void 直接丢弃，其余返回默认值
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
            if (!self)
            {
                YOMK_ERR_POS_LOG("service has been deleted, callback ignored.");
                if constexpr (std::is_void_v<Ret>)
                    return;
                else if constexpr (std::is_same_v<Ret, YomkResponse>)
                    return YomkResponse{YomkResponse::eNo, "service has been deleted, callback ignored."};
                else if constexpr (std::is_same_v<Ret, yomk::ContextChecker::ECheckStatus>)
                    return yomk::ContextChecker::eAccept;
                else
                    return Ret{};
            }
            return func(std::forward<decltype(args)>(args)...);
        };
    }
    void installFunc(const std::string &funcName, YomkServiceFunc func);
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    std::shared_ptr<YomkServicePrivate> m_p;
};