#pragma once

#include "ContextGroup.h"
#include "YomkDefine.h"
#include "YomkServer.h"
using namespace yomk;
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
    YomkResponse create(YomkPkgPtr pkg);
    YomkResponse destroy(YomkPkgPtr pkg);
    YomkResponse get(YomkPkgPtr pkg);
    YomkResponse set(YomkPkgPtr pkg);
    YomkResponse turnOnChecker(YomkPkgPtr pkg);
    YomkResponse turnOffChecker(YomkPkgPtr pkg);
    YomkResponse turnOnMonitor(YomkPkgPtr pkg);
    YomkResponse turnOffMonitor(YomkPkgPtr pkg);
    YomkResponse setChecker(YomkPkgPtr pkg);
    YomkResponse setMonitor(YomkPkgPtr pkg);
    YomkResponse keys(YomkPkgPtr pkg);
    YomkResponse keyInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);

private:
    // 默认组 main：成员直取，不进组表、免查找；完整承载现有 Context 全部功能
    ContextGroup m_main;
};
