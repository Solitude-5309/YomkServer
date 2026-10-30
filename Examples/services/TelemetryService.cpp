#include "services/TelemetryService.h"

int TelemetryService::s_instance = 0;

TelemetryService::TelemetryService(YomkServer* server) : YomkService(server), m_id(++s_instance)
{
    name("/TelemetryService");  // 服务名：URL 前缀，全局唯一
    std::ostringstream oss;
    oss << "留痕服务实例 #" << m_id << " 创建";
    YOMK_INFO_TAG("arm.telemetry", oss.str());
}

TelemetryService::~TelemetryService()
{
    std::ostringstream oss;
    oss << "留痕服务实例 #" << m_id << " 析构";
    YOMK_INFO_TAG("arm.telemetry", oss.str());
    --s_instance;
}

int TelemetryService::init()
{
    // 注册功能函数：注册后即可通过 URL /TelemetryService/record 调用
    installFunc("/record", &TelemetryService::record);

    // 弱绑定注册到 FunctionPool：服务删除后 telemetry_hook 自动失效，不悬垂 this
    YOMK_FUNCTIONPOOL_REGISTER("telemetry_hook", weakFunc(&TelemetryService::record));

    YOMK_INFO_TAG("arm.telemetry", "init: install /record, register weak-bound telemetry_hook");
    return 0;
}

void TelemetryService::deinit()
{
    YOMK_INFO_TAG("arm.telemetry", "deinit: telemetry service offline");
}

YomkResponse TelemetryService::record(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);  // String 为框架内置消息类型
    std::ostringstream oss;
    oss << "留痕记录 [实例#" << m_id << "]：" << str->d;
    YOMK_INFO_TAG("arm.telemetry", oss.str());
    return {YomkResponse::eOk, "trace recorded"};
}
