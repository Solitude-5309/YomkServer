#include "YomkServer.h"
#include "YomkServerPrivate.h"
#include <iostream>
#include <thread>
#include "Modules/Settings/YomkSettings.h"
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
    for(auto& srvName : srvNames)
    {
        YomkService* srv = nullptr;
        if(srvName == "/YomkSettings")
        {
            srv = new YomkSettings(this);
        }
        else if(srvName == "/YomkFunctionPool")
        {
            srv = new YomkFunctionPool(this);
        }
        else if(srvName == "/YomkContext")
        {
            srv = new YomkContext(this);
        }
        else if(srvName == "/YomkEventLoop")
        {
            srv = new YomkEventLoop(this);
        }
        else if(srvName == "/YomkLogger")
        {
            srv = new YomkLogger(this);
        }
        else
        {
            std::cout << u8"[YomkServer::startService] not support service: " << srvName << std::endl;
            continue;
        }

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
    if(!m_p) return;
    m_p->addService(srv);
}

YomkRespond YomkServer::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_p) return YomkRespond();

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        std::cout << u8"[YomkServer::request] url parse error. " << std::endl;
        return YomkRespond();
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        std::cout << u8"[YomkServer::request] url parse error. " << std::endl;
        return YomkRespond();
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        std::cout << u8"[YomkServer::request] url parse error: srv is empty. " << std::endl;
        return YomkRespond();
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        std::cout << u8"[YomkServer::request] url parse error: func is empty" << std::endl;
        return YomkRespond();
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkRespondFunc func)
{
    if(!m_p) return ;

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        std::cout << u8"[YomkServer::asyncRequest] url parse error. " << std::endl;
        return ;
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        std::cout << u8"[YomkServer::asyncRequest] url parse error. " << std::endl;
        return ;
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        std::cout << u8"[YomkServer::asyncRequest] url parse error: srv is empty. " << std::endl;
        return ;
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        std::cout << u8"[YomkServer::asyncRequest] url parse error: func is empty" << std::endl;
        return ;
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

    return ;
}
