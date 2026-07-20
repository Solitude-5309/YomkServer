#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

class MyBoot : public YomkBoot
{
public:
    MyBoot() {}
    int before() override;
    int start() override;
    int after() override;
};
