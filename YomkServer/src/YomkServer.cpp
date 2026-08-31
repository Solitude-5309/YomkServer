#include "YomkServer.h"
#include "YomkServerPrivate.h"
#include <iostream>
#include <thread>
#include <unordered_map>
#include <functional>
#include "Modules/FunctionPool/YomkFunctionPool.h"
#include "Modules/Context/YomkContext.h"
#include "Modules/EventLoop/YomkEventLoop.h"
#include "Modules/Logger/YomkLogger.h"
#include "Modules/ServerInfo/YomkServerInfo.h"

YomkServer::YomkServer()
    : m_p(new YomkServerPrivate())
{
}

int YomkServer::startService(std::vector<std::string> srvNames)
{
    static const std::unordered_map<std::string, std::function<YomkService *(YomkServer *)>> serviceCreators = {
        {"/YomkFunctionPool", [](YomkServer *server)
         { return new YomkFunctionPool(server); }},
        {"/YomkContext", [](YomkServer *server)
         { return new YomkContext(server); }},
        {"/YomkEventLoop", [](YomkServer *server)
         { return new YomkEventLoop(server); }},
        {"/YomkLogger", [](YomkServer *server)
         { return new YomkLogger(server); }},
        {"/YomkServerInfo", [](YomkServer *server)
         { return new YomkServerInfo(server); }}};

    for (auto &srvName : srvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {
            YOMK_ERR_POS_LOG("yomk does not support service: " + srvName);
            continue;
        }

        YomkService *srv = it->second(this);
        srv->name(srvName);

        addService(srv);
    }

    return 0;
}

void YomkServer::addService(YomkService *srv)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return;
    }

    if (!srv)
    {
        YOMK_ERR_POS_LOG("service is null, please check the service.");
        return;
    }

    // 先入表取得 shared_ptr 所有权，保证 init() 内 weak_from_this() 有效
    m_p->addService(srv);

    if (srv->init() != 0)
    {
        YOMK_ERR_POS_LOG("service init error: " + srv->name());
        m_p->delService(srv->name());
        return;
    }
}

int YomkServer::delService(const std::string &srvName)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return -1;
    }

    return m_p->delService(srvName);
}

void YomkServer::shutdown()
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return;
    }

    m_p->shutdown();
}

std::vector<std::string> YomkServer::serviceNames()
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return {};
    }
    return m_p->serviceNames();
}

std::map<std::string, YomkFuncInfo> YomkServer::serviceFuncInfos(const std::string &srvName)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return {};
    }
    return m_p->serviceFuncInfos(srvName);
}

YomkResponse YomkServer::request(const std::string &url, YomkPkgPtr pkg)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return YomkResponse(YomkResponse::eNo, "server is null, please start the server.");
    }

    if (url.empty() || url[0] != '/')
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", please start with /");
        return YomkResponse(YomkResponse::eNo, "url parse error: " + url + ", please start with /");
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", not found service name.");
        return YomkResponse(YomkResponse::eNo, "url parse error: " + url + ", not found service name.");
    }

    std::string srvName = url.substr(0, posEnd);
    if (srvName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: srv is empty. ");
        return YomkResponse(YomkResponse::eNo, "url parse error: srv is empty. ");
    }

    std::string tmpFuncName = url.substr(posEnd);
    if (tmpFuncName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: function name is empty");
        return YomkResponse(YomkResponse::eNo, "url parse error: function name is empty");
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if (!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");
        return;
    }

    if (url.empty() || url[0] != '/')
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", please start with /");
        return;
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", not found service name.");
        return;
    }

    std::string srvName = url.substr(0, posEnd);
    if (srvName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: srv is empty. ");
        return;
    }

    std::string tmpFuncName = url.substr(posEnd);
    if (tmpFuncName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: function name is empty");
        return;
    }

    // 按值捕获 shared_ptr，避免 detach 线程内访问已析构的 this
    std::shared_ptr<YomkServerPrivate> p = m_p;
    std::thread t([srvName, tmpFuncName, pkg, p, func]()
                  {
        if(func)
        {
            func(p->request(srvName, tmpFuncName, pkg));
        }
        else
        {
            p->request(srvName, tmpFuncName, pkg);
        } });
    t.detach();
}
