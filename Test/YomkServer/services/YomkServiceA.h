#pragma once
#include "YomkAPI.h"
#include "msgs/YomkMsgDefine.h"

// 创建一个服务A，用于编写功能集合
class YomkServiceA : public YomkService
{
public:
    YomkServiceA(YomkServer *server);
    virtual ~YomkServiceA() {}

public:
    virtual int init();

private:
    YomkResponse callSkillA(YomkPkgPtr pkg);
};