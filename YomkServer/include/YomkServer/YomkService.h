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
class YomkServerPrivate;
class YomkServicePrivate;

// 服务基类
class YOMKSERVER_EXPORT YomkService : public std::enable_shared_from_this<YomkService>
{
    friend class YomkServerPrivate;

public:
    YomkService(YomkServer *server);
    virtual ~YomkService() {}

public:
    // 设置与获取服务名（URL 前缀）
    void name(const std::string &name);
    std::string name();
    void markDeleted();
    // 检查服务是否已标记注销
    bool deleted() const;

public:
    // 服务初始化（在其中注册功能函数），返回 0 成功
    virtual int init() = 0;
    // 服务注销与资源释放
    virtual void deinit() {}

public:
    // 包装成员函数为 weak_ptr 安全回调，服务注销后自动忽略调用
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
    // 注册功能函数
    void installFunc(const std::string &funcName, YomkServiceFunc func, const std::string &msgName = "");
    // 获取本服务已注册功能函数元信息
    std::map<std::string, YomkFuncInfo> funcInfos();
    // 直接调用本服务的功能函数
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    // 发起同步请求
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    // 发起异步请求
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    std::shared_ptr<YomkServicePrivate> m_p;
};