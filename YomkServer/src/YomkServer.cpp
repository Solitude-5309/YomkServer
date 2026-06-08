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


YomkServer::YomkServer()
    : m_p(new YomkServerPrivate())
{
}

int YomkServer::startService(std::vector<std::string> srvNames)
{
    static const std::unordered_map<std::string, std::function<YomkService*(YomkServer*)>> serviceCreators = {
        {"/YomkFunctionPool", [](YomkServer* server) { return new YomkFunctionPool(server); }},
        {"/YomkContext", [](YomkServer* server) { return new YomkContext(server); }},
        {"/YomkEventLoop", [](YomkServer* server) { return new YomkEventLoop(server); }},
        {"/YomkLogger", [](YomkServer* server) { return new YomkLogger(server); }}
    };

    for(auto& srvName : srvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {   
            YOMK_ERR_POS_LOG("yomk does not support service: " + srvName);  
            continue;
        }

        YomkService* srv = it->second(this);
        srv->name(srvName);

        if (srv->init() != 0)
        {
            return 1;
        }

        addService(srv);
    }

    return 0;
}

void YomkServer::addService(YomkService *srv)
{
    if(!m_p)
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");  
        return;
    }

    m_p->addService(srv);
}

YomkResponse YomkServer::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_p) 
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");  
        return YomkResponse(YomkResponse::eErr, "server is null, please start the server.");
    }

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", please start with /");
        return YomkResponse(YomkResponse::eErr, "url parse error: " + url + ", please start with /");
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", not found service name.");
        return YomkResponse(YomkResponse::eErr, "url parse error: " + url + ", not found service name.");
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: srv is empty. ");  
        return YomkResponse(YomkResponse::eErr, "url parse error: srv is empty. ");
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: function name is empty");
        return YomkResponse(YomkResponse::eErr, "url parse error: function name is empty");
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if(!m_p) 
    {
        YOMK_ERR_POS_LOG("server is null, please start the server.");  
        return;
    }

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", please start with /");
        return;
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", not found service name.");
        return;
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: srv is empty. ");  
        return;
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        YOMK_ERR_POS_LOG("url parse error: function name is empty");
        return;
    }

    std::thread t([srvName, tmpFuncName, pkg, this, func](){
        if(func)
        {
            func(m_p->request(srvName, tmpFuncName, pkg));
        }
        else
        {
            m_p->request(srvName, tmpFuncName, pkg);
        }
    });
    t.detach();
}
