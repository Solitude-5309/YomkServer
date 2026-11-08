#pragma once
#include <string>
#include <vector>

#include "YomkPayload.h"

namespace yomk::payload
{

struct ServerServiceInfo
{
    std::string m_serviceName;      // 服务名（带 / 前缀，如 "/FmtSrv"）
    std::vector<FuncInfo> m_funcs;  // 函数元信息（与 /function 单查同族，m_funcName 带 / 前缀 url 段）
};

}  // namespace yomk::payload
