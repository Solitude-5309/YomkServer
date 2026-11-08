#pragma once
#include <yomk/server/YomkServerApi.h>

using namespace yomk::payload;

class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer* server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    yomk::payload::Response version(YomkMsgPtr pkg);
};
