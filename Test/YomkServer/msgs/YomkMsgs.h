#pragma once
#include "YomkAPI.h"

/**
 * @brief 消息包定义文件
 * 
 * 所有服务间通信的数据结构都在这里集中定义
 * 使用 YomkMsg 宏将普通结构体注册为可在框架中传输的消息包
 */

/**
 * @brief 服务通信消息结构
 * 
 * 定义服务间传递的消息内容，这里只有一个字符串字段
 * 实际业务中可包含任意多个字段
 */
struct MyServiceMsg
{
    std::string content;  // 消息内容
};

/**
 * @brief 注册消息包
 * 
 * 将自定义数据类映射为 YomkMsg 消息包
 * 参数说明：
 * - MyServiceMsg: 自定义数据类（实际的数据结构类型）
 * - YMyServiceMsg: 消息名称（用于框架类型识别和映射，将自定义类映射到YomkMsg体系）
 * - msg: 数据成员变量名（访问时使用 ptr->msg.content）
 * 
 * 注册后，辅助宏均使用消息名称 YMyServiceMsg：
 * - YomkMkPtr(YMyServiceMsg, MyServiceMsg{"data"}) 创建消息包指针
 * - YomkUnPackPkgResponse(pkg, YMyServiceMsg, ptr) 解包消息
 */
YomkMsg(MyServiceMsg, YMyServiceMsg, msg)
