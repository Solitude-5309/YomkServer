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
    YomkRespond load(YomkPkgPtr pkg);
    YomkRespond save(YomkPkgPtr pkg);
    YomkRespond get(YomkPkgPtr pkg);
    YomkRespond set(YomkPkgPtr pkg);
private:
    nlohmann::json m_settings;
};