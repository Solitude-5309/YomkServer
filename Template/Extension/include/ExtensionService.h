#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

class ExtensionService : public YomkService
{
public:
    ExtensionService(YomkServer *server);
    virtual ~ExtensionService() {}
    virtual int init() override;

private:
    YomkResponse version(YomkPkgPtr pkg);
};
