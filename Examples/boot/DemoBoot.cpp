#include "boot/DemoBoot.h"
#include "services/AuditService.h"
#include "services/CalcService.h"

int DemoBoot::before()
{
    // 服务启动前的资源准备：创建 Context、EventLoop、注册公共函数、日志代理等
    YOMK_INFO_TAG("boot", "before: prepare resources before services (none needed here)");
    return 0;
}

int DemoBoot::start()
{
    YOMK_INFO_TAG("boot", "start: register services from creator map");

    // 服务清单：key 为服务名（/ 开头），value 为创建服务实例的 lambda
    // 工程实践中把所有服务都登记在此，按 m_startSrvNames 按需启动
    static const std::map<std::string, std::function<YomkService *()>> serviceCreators = {
        {"/CalcService", []()
         { return new CalcService(YOMK_SERVER_P); }},
        {"/AuditService", []()
         { return new AuditService(YOMK_SERVER_P); }},
    };

    for (const auto &srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {
            YOMK_ERROR_TAG("boot", "unknown service in start list: ", srvName);
            return -1; // 启动清单中出现未知服务，终止启动
        }
        // YOMK_ADD_SERVICE 移交实例所有权并调用服务 init()
        if (YOMK_ADD_SERVICE(it->second(), srvName) != 0)
        {
            return -1;
        }
    }
    return 0;
}

int DemoBoot::after()
{
    // 启动后善后：调用服务接口预热、自启动任务、发送启动完成通知等
    YOMK_INFO_TAG("boot", "after: post-start tasks (warmup, notify, ...)");
    return 0;
}
