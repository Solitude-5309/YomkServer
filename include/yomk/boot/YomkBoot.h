#pragma once
#include "yomk/YomkExport.h"

/**
 * @brief Boot orchestration contract: stages run in the order before -> start -> after
 * @note Consumed via yomk::server().boot_ptr(cfg).run(), ownership is transferred to run; any non-zero stage aborts the sequence
 */
// @zh: 启动编排契约：按 before -> start -> after 顺序执行
// @zh: note 经 yomk::server().boot_ptr(cfg).run() 消费，所有权移交 run；任一阶段非 0 即中止
class YOMK_EXPORT YomkBoot
{
public:
    virtual ~YomkBoot() {}
    /**
     * @brief Pre-start preparation (e.g. load configuration, acquire resources)
     * @return 0 to continue; non-zero aborts the orchestration and propagates as-is
     */
    // @zh: 启动前置准备（如读配置、准备资源）
    // @zh: return 0 继续；非 0 中止编排并原样传播
    virtual int before() = 0;
    /**
     * @brief Startup body (e.g. create and register services)
     * @return 0 to continue; non-zero aborts the orchestration and propagates as-is
     */
    // @zh: 启动主体（如创建并注册服务）
    // @zh: return 0 继续；非 0 中止编排并原样传播
    virtual int start() = 0;
    /**
     * @brief Post-start actions (e.g. mount listeners)
     * @return 0 on success; non-zero aborts the orchestration and propagates as-is
     */
    // @zh: 启动后置动作（如挂载监听）
    // @zh: return 0 成功；非 0 中止并原样传播
    virtual int after() = 0;
};
