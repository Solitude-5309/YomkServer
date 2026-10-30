#include "services/MotionService.h"

MotionService::MotionService(YomkServer* server) : YomkService(server)
{
    name("/MotionService");  // 服务名：URL 前缀，全局唯一
}

int MotionService::init()
{
    // 2 参注册：无消息元数据；4 参注册：声明 /move 期望 MoveCmd 并附函数说明，供自省识别
    installFunc("/echo", &MotionService::echo);
    installFunc("/move", &MotionService::move, "MoveCmd", "运动控制：按指令运动到目标位姿并留痕");
    YOMK_INFO_TAG("arm.motion", "init: install /echo (no meta), /move [MoveCmd] + description");
    return 0;
}

YomkResponse MotionService::echo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);  // String 为框架内置消息类型
    std::ostringstream oss;
    oss << "通讯自检回环：收到 \"" << str->d << "\"";
    YOMK_INFO_TAG("arm.motion", oss.str());
    return {YomkResponse::eOk, "echo success", yomkmsg::String::shared("echo: " + str->d)};
}

YomkResponse MotionService::move(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, MoveCmd, cmdPkg);

    std::ostringstream oss;
    oss << "运动指令执行：目标 (" << cmdPkg->cmd.m_x << ", " << cmdPkg->cmd.m_y << ", " << cmdPkg->cmd.m_z
        << ")mm，倍率 " << cmdPkg->cmd.m_speedRatio;
    YOMK_INFO_TAG("arm.motion", oss.str());

    // 跨服务调用：成员 request() 与全局宏 YOMK_REQUEST 等价，URL 直达留痕工位；
    // 留痕是运动的依赖前置，留痕缺失时失败沿调用链传播
    oss.str("");
    oss << "运动留痕：move -> (" << cmdPkg->cmd.m_x << "," << cmdPkg->cmd.m_y << "," << cmdPkg->cmd.m_z << ")mm";
    YomkResponse traceResp = request("/TelemetryService/record", yomkmsg::String::shared(oss.str()));
    if (traceResp.m_status != YomkResponse::eOk)
    {
        oss.str("");
        oss << "留痕失败：" << traceResp.m_msg;
        YOMK_INFO_TAG("arm.motion", oss.str());
        return {YomkResponse::eNo, "move failed: " + traceResp.m_msg};
    }
    oss.str("");
    oss << "留痕回执：" << traceResp.m_msg;
    YOMK_INFO_TAG("arm.motion", oss.str());

    // 到位回执：m_data 携带强类型 MoveResp，不止能传 String
    MoveResp respData{cmdPkg->cmd.m_x, cmdPkg->cmd.m_y, cmdPkg->cmd.m_z};
    return {YomkResponse::eOk, "move success", yomkmsg::MoveResp::shared(respData)};
}
