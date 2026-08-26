#pragma once
#include <YomkServer/YomkAPI.h>

// ConfigService 消息包
// 配置键：用于 /get 和 /reload
struct ConfigKey
{
    std::string key;
};
YomkMsg(ConfigKey, ConfigKey, req)
    // 访问: ptr->req.key

    // 配置键值：用于 /set
    struct ConfigKeyValue
{
    std::string key;
    std::string value;
};
YomkMsg(ConfigKeyValue, ConfigKeyValue, req)
    // 访问: ptr->req.key, ptr->req.value