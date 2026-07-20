#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

class MyBoot : public YomkBoot
{
public:
    MyBoot(int argc, char *argv[], const std::vector<std::string> &startSrvNames = {})
        : m_argc(argc), m_argv(argv), m_startSrvNames(startSrvNames) {}
    int before() override;
    int start() override;
    int after() override;

private:
    int m_argc;
    char **m_argv;
    std::vector<std::string> m_startSrvNames;
};
