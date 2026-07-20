#pragma once
#include <YomkServer/YomkAPI.h>
#include <nlohmann/json.hpp>
#include "msgs/YomkMsgs.h"

using namespace yomk;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer *server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    YomkResponse loadConfig(YomkPkgPtr pkg);
    YomkResponse getConfig(YomkPkgPtr pkg);
    YomkResponse setConfig(YomkPkgPtr pkg);
    YomkResponse reloadConfig(YomkPkgPtr pkg);

    std::string m_configPath;
    nlohmann::json m_json;
};
