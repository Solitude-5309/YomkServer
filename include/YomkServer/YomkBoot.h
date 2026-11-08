#pragma once
#include "YomkExport.h"

/**
 * @brief 启动编排契约：按 before -> start -> after 顺序执行
 * @note 经 yomk::server().boot_ptr(cfg).run() 消费, 所有权移交 run; 任一阶段非 0 即中止
 */
class YOMK_EXPORT YomkBoot
{
public:
    virtual ~YomkBoot() {}
    /**
     * @brief 启动前置准备（如读配置、准备资源）
     * @return 0 继续; 非 0 中止编排并原样传播
     */
    virtual int before() = 0;
    /**
     * @brief 启动主体（如创建并注册服务）
     * @return 0 继续; 非 0 中止编排并原样传播
     */
    virtual int start() = 0;
    /**
     * @brief 启动后置动作（如挂载监听）
     * @return 0 成功; 非 0 中止并原样传播
     */
    virtual int after() = 0;
};
