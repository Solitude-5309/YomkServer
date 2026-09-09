#pragma once
#include "YomkAPI.h"

/**
 * @file DemoBoot.h
 * @brief 启动编排样例：继承 YomkBoot 组织多服务工程启动
 *
 * YOMK_BOOT(boot) 按固定顺序执行三阶段，任一阶段返回非 0 即终止启动：
 * 1. before()：服务启动前准备资源（Context、EventLoop、公共函数等）
 * 2. start() ：创建并注册服务（各服务 init() 由框架在此阶段调用）
 * 3. after() ：启动后善后（预热、通知、自启动任务等）
 *
 * start() 用 serviceCreators 映射表管理"服务名 -> 创建器"：
 * 全部服务入清单、按需启动；同一个类也可注册为多个实例。
 */
class DemoBoot : public YomkBoot
{
public:
    explicit DemoBoot(const std::vector<std::string> &startSrvNames = {})
        : m_startSrvNames(startSrvNames) {}

    int before() override;
    int start() override;
    int after() override;

private:
    std::vector<std::string> m_startSrvNames; // 本次要启动的服务清单
};
