#include "MyBoot.h"
#include "services/YomkServiceA.h"
#include "services/YomkServiceB.h"

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

int MyBoot::start()
{
    // 现有的全部服务清单
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/YomkServiceA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},
        {"/YomkServiceAA", []()
         { return new YomkServiceA(YOMK_SERVER_P); }},
        {"/YomkServiceBB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }},
        {"/YomkServiceB", []()
         { return new YomkServiceB(YOMK_SERVER_P); }}};

    for (const auto &srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it != serviceCreators.end())
        {
            if (YOMK_ADD_SERVICE(it->second(), srvName) != 0)
                return -1;
        }
    }
    return 0;
}

int MyBoot::after()
{
    // 服务启动后的善后操作
    // 调用服务接口进行服务内部初始化操作
    // 调用服务接口自启动某个任务
    return 0;
}
