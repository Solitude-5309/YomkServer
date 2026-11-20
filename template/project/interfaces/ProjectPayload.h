#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

namespace yomk::payload::prj
{
// ConfigService 消息包
// 配置键：用于 /get 和 /reload
struct ConfigKey
{
    std::string key;
};

// 配置键值：用于 /set
struct ConfigKeyValue
{
    std::string key;
    std::string value;
};
} // namespace yomk::payload::prj