#pragma once

#include "YomkExport.h"
#include "YomkServer.h"

class YomkServerInfo : public YomkService
{
public:
    YomkServerInfo(YomkServer* server);
    virtual ~YomkServerInfo() {}

public:
    virtual int init() override;

private:
    yomk::payload::Response listServices(YomkPkgPtr pkg);
    yomk::payload::Response listFunctions(YomkPkgPtr pkg);
    yomk::payload::Response functionInfo(YomkPkgPtr pkg);
    yomk::payload::Response listAll(YomkPkgPtr pkg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
