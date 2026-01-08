#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include <map>

class YomkFunctionPool : public YomkService
{
public:
    YomkFunctionPool(YomkServer* server);
    ~YomkFunctionPool() {}
public:
    virtual int init() override;
private:
    YomkResponse registerFunction(YomkPkgPtr pkg);
    YomkResponse callFunction(YomkPkgPtr pkg);
private:
    YomkServer* m_server;
    std::map<std::string, YomkFunction> m_functions;
};