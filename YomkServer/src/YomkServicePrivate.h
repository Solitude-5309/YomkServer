#pragma once
#include <string>
#include <map>
#include <shared_mutex>
#include "YomkDefine.h"
#include "YomkPkg.h"

class YomkServer;
class YomkServicePrivate
{
public:
    YomkServicePrivate(YomkServer* server) : m_server(server){}
    ~YomkServicePrivate() {}
public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }
public:
    void installFunc(const std::string& funcName, YomkServiceFunc func);
    YomkRespond invoke(const std::string& funcName, YomkPkgPtr pkg = nullptr);
    YomkRespond request(const std::string& url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string& url, YomkPkgPtr pkg = nullptr, YomkRespondFunc func = nullptr);
protected:
    YomkServer* m_server;
    std::string m_name;
    std::map<std::string, YomkServiceFunc> m_funcMap;
    std::shared_mutex m_funcMapMtx;
};