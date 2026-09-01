#pragma once
#include "YomkAPI.h"
#include "msgs/YomkMsgs.h"

/**
 * @brief 服务A - 示例服务类
 * 
 * 演示如何创建一个 YomkService 服务：
 * 1. 继承 YomkService 基类
 * 2. 在构造函数中设置服务名称（必须以 / 开头，全局唯一）
 * 3. 实现 init() 方法注册功能函数
 * 4. 在 private 中定义具体的业务方法
 * 
 * 本服务演示：
 * - 功能函数注册与调用
 * - 跨服务调用（调用 YomkServiceB）
 */
class YomkServiceA : public YomkService
{
public:
    /**
     * @brief 构造函数
     * @param server YomkServer 实例指针，由框架传入
     */
    YomkServiceA(YomkServer *server);
    virtual ~YomkServiceA() {}

public:
    /**
     * @brief 服务初始化方法（必须实现）
     * 
     * 在此注册服务提供的功能函数
     * 返回 0 表示成功，非 0 表示失败
     */
    virtual int init();

private:
    /**
     * @brief 功能函数 A
     * @param pkg 输入消息包
     * @return YomkResponse 响应结果
     * 
     * 演示：
     * - 解包输入消息
     * - 跨服务调用 YomkServiceB
     * - 返回处理结果
     */
    YomkResponse callSkillA(YomkPkgPtr pkg);
};