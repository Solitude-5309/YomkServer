#pragma once

#include "yomk/server/YomkServer.h"

class YomkServerInfo : public YomkService
{
public:
    YomkServerInfo(YomkServer* server);
    virtual ~YomkServerInfo() {}

public:
    virtual int init() override;

private:
    yomk::payload::Response listServices(YomkMsgPtr msg);
    yomk::payload::Response listFunctions(YomkMsgPtr msg);
    yomk::payload::Response functionInfo(YomkMsgPtr msg);
    yomk::payload::Response listAll(YomkMsgPtr msg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
