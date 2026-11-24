#include "yomk/log/YomkLoggerApi.h"

#include "services/MotionService.h"

MotionService::MotionService(YomkServer* server) : YomkService(server)
{
    name("/MotionService");  // 服务名：URL 前缀，全局唯一
}

int MotionService::init()
{
    // 2 参注册：无消息元数据；4 参注册：声明 /move 期望 MoveCmd 并附函数说明，供自省识别
    installFunc("/echo", &MotionService::echo);
    installFunc("/move", &MotionService::move, yomk::msgs::MoveCmd::kTypeName, "运动控制：按指令运动到目标位姿并留痕");
    yomk::log().tag("arm.motion").info("init: install /echo (no meta), /move [MoveCmd] + description");
    return 0;
}

yomk::payload::Response MotionService::echo(YomkConstMsgPtr msg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(msg);  // String 为框架内置消息类型
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, str.error()};
    }
    std::ostringstream oss;
    oss << "通讯自检回环：收到 \"" << str->d << "\"";
    yomk::log().tag("arm.motion").info(oss.str());
    return {yomk::payload::ResStatus::Succeeded, "echo success", yomk::msgs::String::shared("echo: " + str->d)};
}

yomk::payload::Response MotionService::move(YomkConstMsgPtr msg)
{
    auto cmdMsg = yomk::msgs::unpack<yomk::msgs::MoveCmd>(msg);
    if (!cmdMsg)
    {
        return {yomk::payload::ResStatus::Failed, cmdMsg.error()};
    }

    std::ostringstream oss;
    oss << "运动指令执行：目标 (" << cmdMsg->cmd.x << ", " << cmdMsg->cmd.y << ", " << cmdMsg->cmd.z
        << ")mm，倍率 " << cmdMsg->cmd.speedRatio;
    yomk::log().tag("arm.motion").info(oss.str());

    // 跨服务调用：成员 request() 与全局请求链 yomk::server().url(u).msg(msg).request() 等价，URL 直达留痕工位；
    // 留痕是运动的依赖前置，留痕缺失时失败沿调用链传播
    oss.str("");
    oss << "运动留痕：move -> (" << cmdMsg->cmd.x << "," << cmdMsg->cmd.y << "," << cmdMsg->cmd.z << ")mm";
    yomk::payload::Response traceResp = request("/TelemetryService/record", yomk::msgs::String::shared(oss.str()));
    if (traceResp.status != yomk::payload::ResStatus::Succeeded)
    {
        oss.str("");
        oss << "留痕失败：" << traceResp.msg;
        yomk::log().tag("arm.motion").info(oss.str());
        return {yomk::payload::ResStatus::Failed, "move failed: " + traceResp.msg};
    }
    oss.str("");
    oss << "留痕回执：" << traceResp.msg;
    yomk::log().tag("arm.motion").info(oss.str());

    // 到位回执：data 携带强类型 MoveResp，不止能传 String
    MoveResp respData{cmdMsg->cmd.x, cmdMsg->cmd.y, cmdMsg->cmd.z};
    return {yomk::payload::ResStatus::Succeeded, "move success", yomk::msgs::MoveResp::shared(respData)};
}
