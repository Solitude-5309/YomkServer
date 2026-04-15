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
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "yomk does not support service: " << srvName << std::endl;
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
    if(!m_p)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null, please start the server." << std::endl;
        return;
    }

    m_p->addService(srv);
}

YomkResponse YomkServer::request(const std::string &url, YomkPkgPtr pkg)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null, please start the server."<<std::endl;
        return YomkResponse(YomkResponse::eErr, "server is null, please start the server.");
    }

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: " << url <<", please start with /" << std::endl;
        return YomkResponse(YomkResponse::eErr, "url parse error: " + url + ", please start with /");
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: " << url <<", not found service name." << std::endl;
        return YomkResponse(YomkResponse::eErr, "url parse error: " + url + ", not found service name.");
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: srv is empty. " << std::endl;
        return YomkResponse(YomkResponse::eErr, "url parse error: srv is empty. ");
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: function name is empty" << std::endl;
        return YomkResponse(YomkResponse::eErr, "url parse error: function name is empty");
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    if(!m_p) 
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "server is null, please start the server."<<std::endl;
        return;
    }

    size_t posStart = url.find('/');
    if(posStart == std::string::npos)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: " << url <<", please start with /" << std::endl;
        return;
    }

    size_t posEnd = url.find('/', posStart + 1);
    if(posEnd == std::string::npos)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: " << url <<", not found service name." << std::endl;
        return;
    }

    std::string srvName = url.substr(posStart, posEnd - posStart);
    if(srvName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: srv is empty. " << std::endl;
        return;
    }

    std::string tmpFuncName = url.substr(posEnd);
    if(tmpFuncName.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "url parse error: function name is empty" << std::endl;
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
