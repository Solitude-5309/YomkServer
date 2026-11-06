#include "services/TelemetryService.h"

int TelemetryService::s_instance = 0;

TelemetryService::TelemetryService(YomkServer* server) : YomkService(server), m_id(++s_instance)
{
    name("/TelemetryService");  // 服务名：URL 前缀，全局唯一
    std::ostringstream oss;
    oss << "留痕服务实例 #" << m_id << " 创建";
    yomk::log().tag("arm.telemetry").info(oss.str());
}

TelemetryService::~TelemetryService()
{
    std::ostringstream oss;
    oss << "留痕服务实例 #" << m_id << " 析构";
    yomk::log().tag("arm.telemetry").info(oss.str());
    --s_instance;
}

int TelemetryService::init()
{
    // 注册功能函数：注册后即可通过 URL /TelemetryService/record 调用
    installFunc("/record", &TelemetryService::record);

    // 弱绑定注册到 FunctionPool：服务删除后 telemetry_hook 自动失效，不悬垂 this
    yomk::fn_pool().fn_name("telemetry_hook").fn(weakFunc(&TelemetryService::record)).install();

    yomk::log().tag("arm.telemetry").info("init: install /record, register weak-bound telemetry_hook");
    return 0;
}

void TelemetryService::deinit()
{
    yomk::log().tag("arm.telemetry").info("deinit: telemetry service offline");
}

yomk::payload::Response TelemetryService::record(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unPack<yomk::msgs::String>(pkg);  // String 为框架内置消息类型
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::ostringstream oss;
    oss << "留痕记录 [实例#" << m_id << "]：" << str->d;
    yomk::log().tag("arm.telemetry").info(oss.str());
    return {yomk::payload::ResStatus::Succeeded, "trace recorded"};
}
