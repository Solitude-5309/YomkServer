#pragma once
#include <string>
#include <memory>

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
    // 弱绑定守卫：回调触发时先 lock() 判活，服务已删除则安全丢弃，供外流回调（FunctionPool/EventLoop/Context/异步响应）使用
    YomkServiceFunc weakFunc(YomkServiceFunc func);
    YomkResponseFunc weakFunc(YomkResponseFunc func);
    void installFunc(const std::string &funcName, YomkServiceFunc func);
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    std::shared_ptr<YomkServicePrivate> m_p;
};