#pragma once
#include <yomk/msg/YomkMsg.h>

// ConfigService 消息包
// 配置键：用于 /get 和 /reload
struct ConfigKey
{
    std::string key;
};
YOMK_DEFINE_MSG(::ConfigKey, ConfigKey, req);
    // 访问: ptr->req.key

    // 配置键值：用于 /set
    struct ConfigKeyValue
{
    std::string key;
    std::string value;
};
YOMK_DEFINE_MSG(::ConfigKeyValue, ConfigKeyValue, req);
    // 访问: ptr->req.key, ptr->req.value