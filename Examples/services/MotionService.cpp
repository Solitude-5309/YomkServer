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
    yomk::log().tag("arm.motion").info("init: install /echo (no meta), /move [MoveCmd] + description");
    return 0;
}

yomk::payload::Response MotionService::echo(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);  // String 为框架内置消息类型
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::ostringstream oss;
    oss << "通讯自检回环：收到 \"" << str->d << "\"";
    yomk::log().tag("arm.motion").info(oss.str());
    return {yomk::payload::ResStatus::Succeeded, "echo success", yomk::msgs::String::shared("echo: " + str->d)};
}

yomk::payload::Response MotionService::move(YomkMsgPtr pkg)
{
    auto cmdPkg = yomk::msgs::unpack<yomk::msgs::MoveCmd>(pkg);
    if (!cmdPkg)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not MoveCmd"};
    }

    std::ostringstream oss;
    oss << "运动指令执行：目标 (" << cmdPkg->cmd.m_x << ", " << cmdPkg->cmd.m_y << ", " << cmdPkg->cmd.m_z
        << ")mm，倍率 " << cmdPkg->cmd.m_speedRatio;
    yomk::log().tag("arm.motion").info(oss.str());

    // 跨服务调用：成员 request() 与全局请求链 yomk::server().url(u).msg(pkg).request() 等价，URL 直达留痕工位；
    // 留痕是运动的依赖前置，留痕缺失时失败沿调用链传播
    oss.str("");
    oss << "运动留痕：move -> (" << cmdPkg->cmd.m_x << "," << cmdPkg->cmd.m_y << "," << cmdPkg->cmd.m_z << ")mm";
    yomk::payload::Response traceResp = request("/TelemetryService/record", yomk::msgs::String::shared(oss.str()));
    if (traceResp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        oss.str("");
        oss << "留痕失败：" << traceResp.m_msg;
        yomk::log().tag("arm.motion").info(oss.str());
        return {yomk::payload::ResStatus::Failed, "move failed: " + traceResp.m_msg};
    }
    oss.str("");
    oss << "留痕回执：" << traceResp.m_msg;
    yomk::log().tag("arm.motion").info(oss.str());

    // 到位回执：m_data 携带强类型 MoveResp，不止能传 String
    MoveResp respData{cmdPkg->cmd.m_x, cmdPkg->cmd.m_y, cmdPkg->cmd.m_z};
    return {yomk::payload::ResStatus::Succeeded, "move success", yomk::msgs::MoveResp::shared(respData)};
}
