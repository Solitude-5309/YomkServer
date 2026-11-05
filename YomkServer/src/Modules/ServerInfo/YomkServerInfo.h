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
    yomk::payload::Response listServices(YomkMsgPtr pkg);
    yomk::payload::Response listFunctions(YomkMsgPtr pkg);
    yomk::payload::Response functionInfo(YomkMsgPtr pkg);
    yomk::payload::Response listAll(YomkMsgPtr pkg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
