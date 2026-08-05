#pragma once
#include <YomkServer/YomkAPI.h>
#include <unordered_map>
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
    std::unordered_map<std::string, std::string> m_configMap;
};
