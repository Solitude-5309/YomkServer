#pragma once
#include <yomk/server/YomkServerApi.h>

#include <unordered_map>

#include "interfaces/ProjectMsg.h"

using namespace yomk::payload;

class ConfigService : public YomkService
{
public:
    ConfigService(YomkServer* server);
    virtual ~ConfigService() {}
    virtual int init() override;

private:
    yomk::payload::Response loadConfig(YomkConstMsgPtr msg);
    yomk::payload::Response getConfig(YomkConstMsgPtr msg);
    yomk::payload::Response setConfig(YomkConstMsgPtr msg);
    yomk::payload::Response reloadConfig(YomkConstMsgPtr msg);
    yomk::payload::Response version(YomkConstMsgPtr msg);

    std::string m_configPath;
    std::unordered_map<std::string, std::string> m_configMap;
};
