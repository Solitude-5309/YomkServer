#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "../../YomkSimpleThreadPool.h"
#include "YomkDefine.h"
#include "YomkPkg.h"

class ContextGroup;
typedef std::shared_ptr<ContextGroup> ContextGroupPtr;

// Context 底层实例组：完整承载原 YomkContext 的单组功能（键存储、checker/monitor 门控、自持异步监控池），
// 不继承 YomkService；YomkContext 按组名路由到 ContextGroup 实例（对齐 EventLoop 模块双层写法）
class ContextGroup
{
public:
    struct ContextMonitor
    {
        bool asyncMonitor = false;
        YomkContextMonitorFunc contextMonitorFunc;
    };
    struct Context
    {
        std::string key;
        YomkPkgPtr value;
        YomkContextCheckFunc checker;
        std::vector<ContextMonitor> monitors;
    };

public:
    explicit ContextGroup(std::string name);
    ~ContextGroup();

public:
    // 排空式停止监控池（拒新 -> 排空 -> join）：保证返回前存量异步监控任务已全部执行；stop 幂等，重复调用安全
    void stop();

    // 组名
    const std::string& name() const;

public:
    // 返回：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    int create(const std::string& key, YomkPkgPtr value);
    // 返回：0 成功 / 1 key 不存在
    int destroy(const std::string& key);
    // 命中时 value 填现值，key 为空或未命中时填 defaultValue；返回：0 命中 / 1 key 为空 / 2 key 不存在
    int get(const std::string& key, YomkPkgPtr defaultValue, YomkPkgPtr& value);
    // 返回：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
    int set(const std::string& key, YomkPkgPtr value);
    // 开关无失败分支
    void turnOnChecker();
    void turnOffChecker();
    void turnOnMonitor();
    void turnOffMonitor();
    // 返回：0 成功 / 1 key 为空 / 2 checkFunc 为空 / 3 key 不存在
    int setChecker(const std::string& key, YomkContextCheckFunc checkFunc);
    // 返回：0 成功 / 1 key 为空 / 2 monitorFunc 为空 / 3 key 不存在
    int setMonitor(const std::string& key, YomkContextMonitorFunc monitorFunc, bool asyncMonitor);
    // 键名清单快照
    std::vector<std::string> keys();
    // 命中时填充 info 结构化快照；返回：0 命中 / 1 key 为空 / 2 key 不存在
    int keyInfo(const std::string& key, yomk::ContextKeyInfo& info);
    // 全量键元信息快照
    std::vector<yomk::ContextKeyInfo> listAll();

private:
    std::string m_name;
    std::atomic<bool> m_checkerEnabled;
    std::atomic<bool> m_monitorEnabled;
    std::map<std::string, Context> m_contexts;
    std::shared_mutex m_contextsMutex;
    // 异步监控池：组自持，固定单线程 FIFO；post 在 set 写锁内完成 ⇒ 入队序=提交序 ⇒ 异步通知恒按 set 提交序送达（并发 set 亦然），drain-on-stop 不丢；
    // 锁序不变量 m_contextsMutex -> pool.m_mtx 单向：worker 弹出任务即释放池锁再锁外执行、deinit 不持 contexts 锁调 stop、stop 不持池锁 join，无反向同时持有故无 ABBA；
    // 构造即建池（池 stop 后不可复用，服务删除后重新注册经新对象新构造自然重建），deinit 排空停止
    std::unique_ptr<YomkSimpleThreadPool> m_monitorPool;
};
