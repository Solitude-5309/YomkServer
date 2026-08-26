#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"

class YomkServerInfo : public YomkService
{
public:
    YomkServerInfo(YomkServer *server);
    virtual ~YomkServerInfo() {}

public:
    virtual int init() override;

private:
    YomkResponse listServices(YomkPkgPtr pkg);
    YomkResponse listFunctions(YomkPkgPtr pkg);
    YomkResponse functionInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
