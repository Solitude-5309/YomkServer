#pragma once
#include "YomkAPI.h"

class MyBoot : public YomkBoot
{
public:
    MyBoot(const std::vector<std::string> &startSrvNames = {}) : m_startSrvNames(startSrvNames) {}
    int before();
    int start();
    int after();

private:
    std::vector<std::string> m_startSrvNames; // 将要启动的服务清单，实际业务按需启动
};
