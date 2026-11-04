#pragma once
#include <YomkServer/YomkApi.h>

using namespace yomk::payload;

class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer* server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    yomk::payload::Response version(YomkPkgPtr pkg);
};
