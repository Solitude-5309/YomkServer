#pragma once
#include "YomkAPI.h"

/**
 * @file DemoMsgs.h
 * @brief 示例消息包定义
 *
 * 服务间传递的数据结构在此集中定义：普通结构体 + YomkMsg 宏
 * 即可注册为框架内可传输的消息包。
 *
 * YomkMsg(数据类型, 消息名, 成员变量名) 三参数含义：
 * - 数据类型：实际承载数据的结构体
 * - 消息名：框架内类型识别名（供自省/校验用），注册后各辅助宏均以它为名
 * - 成员变量名：包内访问数据的入口，如 ptr->req.a
 */

// 加法请求：配合 3 参 YomkInstallFunc 声明元数据，供自省识别期望消息类型
struct AddReq
{
    std::int32_t a;
    std::int32_t b;
};

// 加法响应：演示回包 m_data 可传自定义强类型，不局限于 String
struct AddResp
{
    std::int64_t result;
};

// 注册后即可使用：
// - 创建：YomkMkPtr(AddReq, AddReq{1, 2})
// - 解包：YomkUnPackPkgResponse(pkg, AddReq, ptr)，通过 ptr->req.a 访问
YomkMsg(AddReq, AddReq, req) YomkMsg(AddResp, AddResp, resp)
