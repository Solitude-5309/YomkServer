#pragma once
#include <YomkServer/YomkApi.h>

#include <unordered_map>

#include "msgs/YomkMsgs.h"

using namespace yomk;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer* server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    yomk::Response loadConfig(YomkPkgPtr pkg);
    yomk::Response getConfig(YomkPkgPtr pkg);
    yomk::Response setConfig(YomkPkgPtr pkg);
    yomk::Response reloadConfig(YomkPkgPtr pkg);
    yomk::Response version(YomkPkgPtr pkg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
