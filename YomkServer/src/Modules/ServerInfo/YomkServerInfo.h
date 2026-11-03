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
    yomk::Response listServices(YomkPkgPtr pkg);
    yomk::Response listFunctions(YomkPkgPtr pkg);
    yomk::Response functionInfo(YomkPkgPtr pkg);
    yomk::Response listAll(YomkPkgPtr pkg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
