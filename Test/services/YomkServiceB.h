#pragma once
#include "YomkAPI.h"
#include "msgs/YomkMsgs.h"

/**
 * @brief 服务B - 被调用服务示例
 * 
 * 演示如何创建一个被其他服务调用的 YomkService：
 * - 提供功能函数供外部调用
 * - 处理请求并返回响应
 * 
 * 本服务被 YomkServiceA 跨服务调用
 */
class YomkServiceB : public YomkService
{
public:
    /**
     * @brief 构造函数
     * @param server YomkServer 实例指针
     */
    YomkServiceB(YomkServer *server);
    virtual ~YomkServiceB() {}

public:
    /**
     * @brief 服务初始化方法
     * 
     * 注册服务提供的功能函数
     */
    virtual int init();

private:
    /**
     * @brief 功能函数 B
     * @param pkg 输入消息包
     * @return YomkResponse 响应结果
     * 
     * 演示：
     * - 解包输入消息
     * - 处理业务逻辑
     * - 返回处理结果
     */
    YomkResponse callSkillB(YomkPkgPtr pkg);
};