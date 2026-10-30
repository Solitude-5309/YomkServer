#pragma once
#include "YomkAPI.h"

/**
 * @file ArmMsgs.h
 * @brief 机械臂控制示例消息包定义
 *
 * 服务间传递的数据结构在此集中定义：普通结构体 + YomkMsg 宏
 * 即可注册为框架内可传输的消息包。
 *
 * YomkMsg(数据类型, 消息名, 成员变量名) 三参数含义：
 * - 数据类型：实际承载数据的结构体
 * - 消息名：框架内类型识别名（供自省/校验用），注册后各辅助宏均以它为名
 * - 成员变量名：包内访问数据的入口，如 ptr->cmd.m_x
 */

// 运动指令：末端目标位姿与速度倍率
struct MoveCmd
{
    double m_x;           // 目标 X（mm）
    double m_y;           // 目标 Y（mm）
    double m_z;           // 目标 Z（mm）
    double m_speedRatio;  // 速度倍率 0.0~1.0
};

// 运动回执：到位反馈（演示回包 m_data 可传自定义强类型，不局限于 String）
struct MoveResp
{
    double m_reachX;  // 到位 X（mm）
    double m_reachY;  // 到位 Y（mm）
    double m_reachZ;  // 到位 Z（mm）
};

// 注册后即可使用：
// - 创建：yomk::MoveCmd_::shared(moveCmd)（聚合初始化先赋具名变量，含逗号不能内联进宏）
// - 解包：YomkUnPackPkgResponse(pkg, MoveCmd, ptr)，通过 ptr->cmd.m_x 访问
YomkMsg(MoveCmd, MoveCmd, cmd) 
YomkMsg(MoveResp, MoveResp, resp)
