#pragma once
#include <string>
#include <map>
#include <memory>
#include <shared_mutex>
#include "YomkPkg.h"

class YomkService;
class YomkServerPrivate
{
public:
    YomkServerPrivate() {}
    ~YomkServerPrivate() {}
public:
    void addService(YomkService* srv);
    YomkResponse request(const std::string& srvName, const std::string& funcName, YomkPkgPtr pkg = nullptr);
private:
    std::map<std::string, std::shared_ptr<YomkService>> m_serviceMap;
    std::shared_mutex m_serviceMapMtx;
};
