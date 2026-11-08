#pragma once
#include <string>
#include <vector>

#include "YomkServerPayload.h"

namespace yomk::payload
{

struct FuncInfo
{
    std::string m_funcName;     // 功能函数名（/开头）
    std::string m_msgName;      // 期望消息类型名（installFunc 第三参声明，可为空）
    std::string m_description;  // 函数说明（installFunc 第四参声明，内容使用者自定义，可为空）
};

struct ServerServiceInfo
{
    std::string m_serviceName;      // 服务名（带 / 前缀，如 "/FmtSrv"）
    std::vector<FuncInfo> m_funcs;  // 函数元信息（与 /function 单查同族，m_funcName 带 / 前缀 url 段）
};

}  // namespace yomk::payload
