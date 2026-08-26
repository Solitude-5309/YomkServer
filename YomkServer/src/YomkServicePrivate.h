#pragma once
#include <string>
#include <map>
#include <memory>
#include <shared_mutex>
#include "YomkDefine.h"
#include "YomkPkg.h"

class YomkServer;
class YomkServicePrivate
{
public:
    YomkServicePrivate(std::weak_ptr<YomkServer> server) : m_weakServer(server) {}
    ~YomkServicePrivate() {}

public:
    void name(const std::string &name) { m_name = name; }
    std::string name() { return m_name; }

public:
    void installFunc(const std::string &funcName, YomkServiceFunc func, const std::string &msgName = "");
    YomkResponse invoke(const std::string &funcName, YomkPkgPtr pkg = nullptr);
    std::map<std::string, YomkFuncInfo> funcInfos();
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

protected:
    std::weak_ptr<YomkServer> m_weakServer;
    std::string m_name;
    std::map<std::string, YomkServiceFunc> m_funcMap;
    std::map<std::string, std::string> m_funcMsgMap;
    std::shared_mutex m_funcMapMtx;
};