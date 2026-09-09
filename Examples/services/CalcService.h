#pragma once
#include "YomkAPI.h"
#include "msgs/DemoMsgs.h"

/**
 * @file CalcService.h
 * @brief 计算服务：演示功能注册、强类型回包与跨服务调用
 *
 * - /echo：最简请求回环（2 参注册，无元数据）
 * - /add ：3 参注册声明 AddReq 元数据；内部用成员 request() 跨服务
 *          调用 /AuditService/audit，演示调用链与失败传播
 *
 * 服务内部调用其他服务用成员函数 request("/服务名/函数名", pkg)，
 * 与全局宏 YOMK_REQUEST 等价，但无需获取全局单例。
 */
class CalcService : public YomkService
{
public:
    explicit CalcService(YomkServer *server);

    int init() override;

    // 最简回环：收到字符串原样回显
    YomkResponse echo(YomkPkgPtr pkg);

    // 加法：先跨服务审计，再返回 AddResp 强类型回包
    YomkResponse add(YomkPkgPtr pkg);
};
