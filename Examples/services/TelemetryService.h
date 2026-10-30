#pragma once
#include "YomkAPI.h"

/**
 * @file TelemetryService.h
 * @brief 留痕服务：被其他服务调用的最小服务样例（机械臂运行黑匣子）
 *
 * 定义一个服务只需三步：
 * 1. 继承 YomkService，构造函数中 name("/TelemetryService") 起名
 *    （必须 / 开头、全局唯一，作为该服务所有功能函数的 URL 前缀）
 * 2. 实现 init()，用 YomkInstallFunc 注册功能函数
 * 3. 功能函数签名统一为 YomkResponse(YomkPkgPtr pkg)
 *
 * 本服务额外演示弱绑定安全：init 中用 weakFunc(&TelemetryService::record)
 * 把成员函数注册到 FunctionPool，服务删除后回调立即失效，不会悬垂崩溃。
 */
class TelemetryService : public YomkService
{
public:
    explicit TelemetryService(YomkServer* server);
    ~TelemetryService() override;

    int init() override;
    void deinit() override;

    // 留痕功能函数：URL 为 /TelemetryService/record，记录一条运动留痕并回执
    YomkResponse record(YomkPkgPtr pkg);

private:
    int m_id;               // 本实例编号：日志用它自证"同名替换"动的是不同实例
    static int s_instance;  // 实例计数：构造 +1、析构 -1
};
