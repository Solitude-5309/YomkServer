#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "yomk/YomkDiag.h"
#include "yomk/srv_info/YomkServerInfoPayload.h"
#include "yomk/server/YomkServerPayload.h"

class YomkServer;
class YomkServicePrivate
{
public:
    YomkServicePrivate(std::weak_ptr<YomkServer> server) : m_weakServer(server) {}
    ~YomkServicePrivate() {}

public:
    void name(const std::string& name);
    std::string name() { return m_name; }
    // 框架内部使用:入表注册后锁定服务名,此后 name() 改名被拒绝(保证与 service map 键一致)
    void markRegistered() { m_registered = true; }
    // 框架内部使用:服务被删除/同名替换时置位(deinit 之前,拦截迟到回调);弱绑定判活为双层
    // (弱引用 + 此标志),置位后在途回调立即丢弃.例外:shutdown 排空期不置位,回调照常执行
    void markDeleted() { m_deleted.store(true); }
    bool deleted() const { return m_deleted.load(); }

public:
    void installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
                     const std::string& description = "");
    yomk::payload::Response invoke(std::string_view funcName, YomkMsgPtr pkg = nullptr);
    std::map<std::string, yomk::payload::FuncInfo> funcInfos();
    yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg = nullptr);
    void asyncRequest(std::string_view url, YomkMsgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

protected:
    std::weak_ptr<YomkServer> m_weakServer;
    std::string m_name;
    // 入表注册后为 true,锁定服务名避免与 service map 键不一致
    bool m_registered = false;
    // 删除/同名替换后为 true,weakFunc 判活叠加此标志(删除即停)
    std::atomic<bool> m_deleted{false};
    std::map<std::string, YomkServiceFunc, std::less<>> m_funcMap;
    std::map<std::string, yomk::payload::FuncInfo, std::less<>> m_funcInfoMap; // funcName -> 函数信息(类型名 + 说明)
    std::shared_mutex m_funcMapMtx;
};