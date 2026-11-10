#pragma once
#include <string>
#include <vector>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload
{

/** @brief Function metadata (for introspection) */
// @zh: 功能函数元信息（内省用）
struct FuncInfo
{
    std::string funcName;     // 功能函数名(/开头)
    std::string msgName;      // 期望消息类型名(installFunc 第三参声明,可为空)
    std::string description;  // 函数说明(installFunc 第四参声明,内容使用者自定义,可为空)
};

/** @brief Service-level topology entry (service name + function list) */
// @zh: 服务级拓扑条目（服务名 + 函数清单）
struct ServerServiceInfo
{
    std::string serviceName;      // 服务名(带 / 前缀,如 "/FmtSrv")
    std::vector<FuncInfo> funcs;  // 函数元信息(与 /function 单查同族,funcName 带 / 前缀 url 段)
};

}  // namespace yomk::payload
