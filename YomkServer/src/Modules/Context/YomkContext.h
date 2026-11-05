#pragma once

#include "ContextGroup.h"
#include "YomkServer.h"
#include <map>
#include <shared_mutex>
class YomkContext : public YomkService
{
public:
    YomkContext(YomkServer* server);
    virtual ~YomkContext() {}

public:
    virtual int init() override;
    // 停止 main 组监控池（拒新 -> 排空 -> join）：保证关闭/删除返回前存量异步监控任务已全部执行；
    // stop 幂等，重复 deinit 安全
    virtual void deinit() override;

private:
    yomk::payload::Response create(YomkMsgPtr pkg);
    yomk::payload::Response destroy(YomkMsgPtr pkg);
    yomk::payload::Response get(YomkMsgPtr pkg);
    yomk::payload::Response set(YomkMsgPtr pkg);
    yomk::payload::Response turnOnChecker(YomkMsgPtr pkg);
    yomk::payload::Response turnOffChecker(YomkMsgPtr pkg);
    yomk::payload::Response turnOnMonitor(YomkMsgPtr pkg);
    yomk::payload::Response turnOffMonitor(YomkMsgPtr pkg);
    yomk::payload::Response setChecker(YomkMsgPtr pkg);
    yomk::payload::Response setMonitor(YomkMsgPtr pkg);
    yomk::payload::Response groups(YomkMsgPtr pkg);
    yomk::payload::Response keys(YomkMsgPtr pkg);
    yomk::payload::Response keyInfo(YomkMsgPtr pkg);
    yomk::payload::Response listAll(YomkMsgPtr pkg);

private:
    // 组路由辅助：main 组成员直取；自定义组按名惰性自动创建（不支持销毁）；
    // 失败时 resp 携带错误响应并返回 nullptr
    ContextGroup* findGroup(const std::string& groupName, yomk::payload::Response& resp);

    // 默认组 main：成员直取，不进组表、免查找；完整承载现有 Context 全部功能
    ContextGroup m_main;

    // 自定义组表：按组名惰性自动创建、不支持销毁（map 只增不减）；
    // 组名唯一由 map key 天然保证，同名寻址即复用同组
    std::map<std::string, ContextGroupPtr> m_customGroup;
    std::shared_mutex m_customGroupMutex;
};
