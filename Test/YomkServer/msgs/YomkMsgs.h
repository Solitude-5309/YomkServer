#pragma once
#include "YomkAPI.h"

// 创建一个消息包，用于服务间通信
struct MyServiceMsg
{
    std::string content;
};
YomkMsg(MyServiceMsg, MyServiceMsg, serviceMsg)