#include "YomkLogApi.h"

#include "boot/ArmBoot.h"

#include "services/MotionService.h"
#include "services/TelemetryService.h"

int ArmBoot::before()
{
    // 服务启动前的资源准备：创建 Context、EventLoop、注册公共函数、日志代理等
    yomk::log().tag("arm.boot").info("before: prepare resources before services (none needed here)");
    return 0;
}

int ArmBoot::start()
{
    yomk::log().tag("arm.boot").info("start: register services from creator map");

    // 服务清单：key 为服务名（/ 开头），value 为创建服务实例的 lambda
    // 工程实践中把所有服务都登记在此，按 m_startSrvNames 按需启动
    static const std::map<std::string, std::function<YomkService*()>> serviceCreators = {
        {"/MotionService", []() { return new MotionService(yomk::server().instance().get()); }},
        {"/TelemetryService", []() { return new TelemetryService(yomk::server().instance().get()); }},
    };

    for (const auto& srvName : m_startSrvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {
            yomk::log().tag("arm.boot").error("unknown service in start list: ", srvName);
            return -1;  // 启动清单中出现未知服务，终止启动
        }
        // yomk::server().add() 移交实例所有权并调用服务 init()
        if (yomk::server().srv_name(srvName).srv_ptr(it->second()).add() != 0)
        {
            return -1;
        }
    }
    return 0;
}

int ArmBoot::after()
{
    // 启动后善后：调用服务接口预热、自启动任务、发送启动完成通知等
    yomk::log().tag("arm.boot").info("after: post-start tasks (warmup, notify, ...)");
    return 0;
}
