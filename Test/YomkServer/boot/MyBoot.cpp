#include "MyBoot.h"
#include "services/YomkServiceA.h"
#include "services/YomkServiceB.h"

/**
 * @brief 服务启动前初始化
 * 
 * 在此阶段创建服务启动前必需的资源：
 * - Context: 全局共享状态
 * - EventLoop: 事件循环
 * - FunctionPool: 公共函数池
 * - 日志代理等
 * 
 * 返回 0 表示成功，非 0 将终止启动流程
 */
int MyBoot::before()
{
    // 服务启动前的初始化操作
    // 服务启动前创建CONTEXT，确保在服务启动时能够访问上下文
    // 服务启动前创建EVENTLOOP，确保在服务启动时能够使用特定的事件循环
    // 服务启动前注册功能函数到FUNCTION_POOL，确保在服务启动时能够访问功能函数
    // 服务启动前创建YOMK_SET_CONSOLE_LOG_PROXY，确保在服务启动时能够触发日志代理
    // 服务启动前创建其他必要的资源，确保在服务启动时能够使用
    return 0;
}

/**
 * @brief 注册并启动服务
 * 
 * 使用服务创建器映射表管理服务实例：
 * - key: 服务名称（必须以 / 开头）
 * - value: 创建服务实例的 lambda 函数
 * 
 * 根据 m_startSrvNames 列表按需启动服务
 * 
 * 返回 0 表示成功，非 0 将终止启动流程
 */
int MyBoot::start()
{
    // 现有的全部服务清单
    // 使用 static const 避免重复创建
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/YomkServiceA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},  // 创建服务A实例
        {"/YomkServiceAA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},  // 同一个类可以注册多个实例
        {"/YomkServiceBB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }},  // 同一个类可以注册多个实例
        {"/YomkServiceB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }}};

    // 遍历需要启动的服务列表
    for (const auto &srvName : m_startSrvNames)
    {
        // 在服务清单中查找对应的创建器
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            // 创建服务实例并注册到框架
            // YOMK_ADD_SERVICE 会调用服务的 init() 方法
            if (YOMK_ADD_SERVICE(it->second(), srvName) != 0)
                return -1;  // 注册失败，终止启动
        }
    }
    return 0;
}

/**
 * @brief 服务启动后善后
 * 
 * 在此阶段可以：
 * - 调用服务接口进行服务内部初始化
 * - 调用服务接口自启动某个任务
 * - 发送启动完成通知等
 * 
 * 返回 0 表示成功，非 0 将终止启动流程
 */
int MyBoot::after()
{
    // 服务启动后的善后操作
    // 调用服务接口进行服务内部初始化操作
    // 调用服务接口自启动某个任务
    return 0;
}
