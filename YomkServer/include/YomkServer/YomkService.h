#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "YomkExport.h"
#include "YomkMsgs.h"

class YomkServer;
class YomkServerPrivate;
class YomkServicePrivate;

class YOMKSERVER_EXPORT YomkService : public std::enable_shared_from_this<YomkService>
{
    friend class YomkServerPrivate;

public:
    YomkService(YomkServer* server);
    virtual ~YomkService() {}

public:
    void name(const std::string& name);
    std::string name();
    bool isDeleted() const;

public:
    virtual int init() = 0;
    virtual void deinit() {}

protected:
    template <typename Class, typename Res, typename Arg>
    void installFunc(const std::string& funcName, Res (Class::*func)(Arg), const std::string& msgName = "",
                     const std::string& description = "")
    {
        installFunc(funcName, weakFunc(func), msgName, description);
    }
    std::map<std::string, yomk::payload::FuncInfo> funcInfos();
    yomk::payload::Response invoke(const std::string& funcName, YomkMsgPtr pkg = nullptr);
    yomk::payload::Response request(const std::string& url, YomkMsgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkMsgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
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
            YOMK_ERR_POS_LOG(
                "weakFunc called before service is owned by server (in constructor?), callback will never fire!");
        }
        return [weakSelf, func](auto&&... args) -> decltype(auto)
        {
            using Ret = decltype(func(args...));
            auto self = weakSelf.lock();
            if (!self || self->isDeleted())
            {
                YOMK_ERR_POS_LOG("service has been deleted or unregistered, callback ignored.");
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