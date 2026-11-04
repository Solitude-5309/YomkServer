#pragma once

#include <map>
#include <mutex>
#include <shared_mutex>

#include "YomkExport.h"
#include "YomkServer.h"

class YomkFunctionPool : public YomkService
{
public:
    YomkFunctionPool(YomkServer* server);
    virtual ~YomkFunctionPool() {}

public:
    virtual int init() override;

private:
    yomk::payload::Response registerFunction(YomkPkgPtr pkg);
    yomk::payload::Response unRegisterFunction(YomkPkgPtr pkg);
    yomk::payload::Response callFunction(YomkPkgPtr pkg);
    yomk::payload::Response funcNames(YomkPkgPtr pkg);
    yomk::payload::Response funcInfo(YomkPkgPtr pkg);
    yomk::payload::Response listAll(YomkPkgPtr pkg);

private:
    // 池中函数条目：函数本体 + 期望消息类型名 + 函数说明（内省元数据，均可为空）
    struct PoolFuncInfo
    {
        YomkServiceFunc m_func;
        std::string m_msgName;
        std::string m_description;
    };

private:
    std::map<std::string, PoolFuncInfo> m_functions;
    std::shared_mutex m_functionsMutex;
};