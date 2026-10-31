#pragma once
#include "YomkAPI.h"
#include "msgs/ArmMsgs.h"

/**
 * @file MotionService.h
 * @brief 运动服务：机械臂运动控制的业务服务样例
 *
 * - /echo：通讯自检回环（2 参注册，无元数据）
 * - /move ：4 参注册声明 MoveCmd 元数据与函数说明；内部用成员 request()
 *           跨服务调用 /TelemetryService/record 留痕，演示调用链与失败传播
 *
 * 服务内部调用其他服务用成员函数 request("/服务名/函数名", pkg)，
 * 与全局宏 YomkAPI::request() 等价，但无需获取全局单例。
 */
class MotionService : public YomkService
{
public:
    explicit MotionService(YomkServer* server);

    int init() override;

    // 通讯自检：收到字符串原样回显
    YomkResponse echo(YomkPkgPtr pkg);

    // 运动指令：先跨服务留痕，再返回 MoveResp 强类型到位回执
    YomkResponse move(YomkPkgPtr pkg);
};
