#pragma once
#include <string>
#include <memory>

#include "YomkPkg.h"
#include "YomkDefine.h"

class YomkServer;
class YomkServicePrivate;
class YOMKSERVER_EXPORT YomkService
{
public:
    YomkService(YomkServer* server);
    ~YomkService() {}
public:
    void name(const std::string& name);
    std::string name();
public:
    virtual int init() = 0;
public:
    void installFunc(const std::string& funcName, YomkServiceFunc func);
    YomkResponse invoke(const std::string& funcName, YomkPkgPtr pkg = nullptr);
    YomkResponse request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
protected:
    std::shared_ptr<YomkServicePrivate> m_p;
};