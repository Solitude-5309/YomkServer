#pragma once
#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include "YomkDefine.h"
#include "YomkPkg.h"

class YomkServer;
class YomkServicePrivate
{
public:
    YomkServicePrivate(std::weak_ptr<YomkServer> server) : m_weakServer(server) {}
    ~YomkServicePrivate() {}

public:
    void name(const std::string &name);
    std::string name() { return m_name; }
    // 框架内部使用：入表注册后锁定服务名，此后 name() 改名被拒绝（保证与 service map 键一致）
    void markRegistered() { m_registered = true; }
    // 框架内部使用（第十六轮）：服务被删除/同名替换时置位，弱绑定回调据此立即失效（删除即停）；
    // shutdown 排空语义不置位，排空期回调照常执行。置位在 deinit() 之前，保证 deinit 期间迟到回调已被拦截
    void markDeleted() { m_deleted.store(true); }
    bool deleted() const { return m_deleted.load(); }

public:
    void installFunc(const std::string &funcName, YomkServiceFunc func, const std::string &msgName = "");
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    std::map<std::string, YomkFuncInfo> funcInfos();
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
    // 投递任意任务到服务器内部异步线程池：服务器已销毁/已关闭/池已停止返回 false
    bool postAsyncTask(std::function<void()> task);

protected:
    std::weak_ptr<YomkServer> m_weakServer;
    std::string m_name;
    // 入表注册后为 true，锁定服务名避免与 service map 键不一致
    bool m_registered = false;
    // 删除/同名替换后为 true，weakFunc 判活叠加此标志（第十六轮）
    std::atomic<bool> m_deleted{false};
    std::map<std::string, YomkServiceFunc> m_funcMap;
    std::map<std::string, std::string> m_funcMsgMap;
    std::shared_mutex m_funcMapMtx;
};