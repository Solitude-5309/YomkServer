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
    yomk::payload::Response listServices(YomkConstMsgPtr msg);
    yomk::payload::Response listFunctions(YomkConstMsgPtr msg);
    yomk::payload::Response functionInfo(YomkConstMsgPtr msg);
    yomk::payload::Response listAll(YomkConstMsgPtr msg);

private:
    std::weak_ptr<YomkServer> m_weakServer;
};
