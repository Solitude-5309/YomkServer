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
    // 设置服务名：须在注册（addService）前调用，注册后改名将被拒绝（保证与 service map 键一致）
    void name(const std::string &name);
    std::string name();
    // 框架内部使用（addService 入表后调用）：锁定服务名，用户代码不应调用
    void markRegistered();
    // 框架内部使用（第十六轮）：服务被删除/同名替换时置位注销标志，弱绑定回调据此立即失效；
    // 用户代码不应调用；deleted() 供判活查询（删除/替换后为 true，shutdown 排空语义不置位）
    void markDeleted();
    bool deleted() const;

public:
    virtual int init() = 0;
    virtual void deinit() {}

public:
    // 弱绑定守卫（泛型模板），供外流回调（功能函数/FunctionPool/EventLoop/Context checker·monitor/异步响应）使用。
    // 判活双层（第十六轮）：引用计数（weak_from_this().lock()）+ 注销标志（deleted()）。
    // 删除即停：YOMK_DEL_SERVICE / 同名替换置位注销标志后，即使在途请求仍持 shared_ptr 副本，
    // 弱绑定回调也立即丢弃（不再等到引用归零）；YOMK_SHUTDOWN 走排空语义不置位，排空期回调照常执行。
    // 子类仍须在 deinit() 中停止非弱绑定路径的生产者（线程/定时器/外部注册）。
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
    // 投递任意任务到服务器内部异步线程池：成功返回 true；服务器已销毁/已关闭/池已停止返回 false
    bool postAsyncTask(std::function<void()> task);

private:
    std::shared_ptr<YomkServicePrivate> m_p;
};