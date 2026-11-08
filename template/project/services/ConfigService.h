#pragma once
#include <yomk/server/YomkServerApi.h>

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
    yomk::payload::Response loadConfig(YomkMsgPtr pkg);
    yomk::payload::Response getConfig(YomkMsgPtr pkg);
    yomk::payload::Response setConfig(YomkMsgPtr pkg);
    yomk::payload::Response reloadConfig(YomkMsgPtr pkg);
    yomk::payload::Response version(YomkMsgPtr pkg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
