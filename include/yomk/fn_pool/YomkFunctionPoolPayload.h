#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

/** @brief Function registration payload (function name + implementation + metadata) */
// @zh: 功能函数注册载荷（函数名 + 实现 + 元数据）
struct Function
{
    std::string m_funcName;
    YomkServiceFunc m_func;
    std::string m_msgName;
    std::string m_description;
};

/** @brief Function call payload (function name + message) */
// @zh: 功能函数调用载荷（函数名 + 消息包）
struct CallFunction
{
    std::string m_funcName;
    YomkMsgPtr m_pkg;
};

}  // namespace yomk::payload
