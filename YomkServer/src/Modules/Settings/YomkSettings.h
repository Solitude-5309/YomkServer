#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include "json.hpp"

class YomkSettings : public YomkService
{
public:
    YomkSettings(YomkServer* server);
    ~YomkSettings() {}
public:
    virtual int init() override;
private:
    YomkResponse load(YomkPkgPtr pkg);
    YomkResponse save(YomkPkgPtr pkg);
    YomkResponse get(YomkPkgPtr pkg);
    YomkResponse set(YomkPkgPtr pkg);
private:
    nlohmann::json m_settings;
};