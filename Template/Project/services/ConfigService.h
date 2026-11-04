#pragma once
#include <YomkServer/YomkApi.h>

#include <unordered_map>

#include "msgs/YomkMsgs.h"

using namespace yomk::payload;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer* server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    yomk::payload::Response loadConfig(YomkPkgPtr pkg);
    yomk::payload::Response getConfig(YomkPkgPtr pkg);
    yomk::payload::Response setConfig(YomkPkgPtr pkg);
    yomk::payload::Response reloadConfig(YomkPkgPtr pkg);
    yomk::payload::Response version(YomkPkgPtr pkg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
