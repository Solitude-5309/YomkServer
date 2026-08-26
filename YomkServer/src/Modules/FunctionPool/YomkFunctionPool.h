#pragma once

#include "YomkServer.h"
#include "YomkDefine.h"
#include <map>
#include <mutex>
#include <shared_mutex>

class YomkFunctionPool : public YomkService
{
public:
    YomkFunctionPool(YomkServer *server);
    virtual ~YomkFunctionPool() {}

public:
    virtual int init() override;

private:
    YomkResponse registerFunction(YomkPkgPtr pkg);
    YomkResponse unRegisterFunction(YomkPkgPtr pkg);
    YomkResponse callFunction(YomkPkgPtr pkg);
    YomkResponse funcNames(YomkPkgPtr pkg);
    YomkResponse funcInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);

private:
    // 池中函数条目：函数本体 + 期望消息类型名（内省元数据，可为空）
    struct PoolFuncInfo
    {
        YomkServiceFunc m_func;
        std::string m_msgName;
    };

private:
    std::map<std::string, PoolFuncInfo> m_functions;
    std::shared_mutex m_functionsMutex;
};