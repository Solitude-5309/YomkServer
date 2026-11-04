#pragma once
#include <YomkServer/YomkApi.h>

using namespace yomk;

class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer* server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    yomk::Response version(YomkPkgPtr pkg);
};
