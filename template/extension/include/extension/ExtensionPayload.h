#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

// Extension 扩展专属载荷（namespace yomk::payload::ext，扩展专属子命名空间，不占 yomk::payload 根）。
// 当前模板仅 /version 接口（返回值复用标准类型 msgs::String），暂无专属载荷；
// 新增业务数据结构在此定义，形态参考 yomk/fn_pool/YomkFunctionPoolPayload.h：
//
namespace yomk::payload::ext
{
struct ExtTask
{
    std::string taskName;
    YomkMsgPtr pkg;
};
}  // namespace yomk::payload::ext
