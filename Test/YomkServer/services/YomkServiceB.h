#pragma once
#include "YomkAPI.h"
#include "msgs/YomkMsgDefine.h"

class YomkServiceB : public YomkService
{
public:
    YomkServiceB(YomkServer *server);
    virtual ~YomkServiceB() {}

public:
    virtual int init();

private:
    YomkResponse callSkillB(YomkPkgPtr pkg);
};