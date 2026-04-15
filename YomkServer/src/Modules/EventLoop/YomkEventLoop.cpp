#include "YomkEventLoop.h"
#include <iostream>

YomkEventLoop::YomkEventLoop(YomkServer *server)
    : YomkService(server)
{
    name("/YomkEventLoop");
}

int YomkEventLoop::init()
{
    YomkInstallFunc("/start", YomkEventLoop::start);
    YomkInstallFunc("/stop", YomkEventLoop::stop);
    YomkInstallFunc("/post", YomkEventLoop::post);
    YomkInstallFunc("/post_wait", YomkEventLoop::postWait);
    YomkInstallFunc("/destroy", YomkEventLoop::destroy);
    return 0;
}

YomkResponse YomkEventLoop::start(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YEventloop", YEventloop, yEventloop);
    if(!yEventloop)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YEventloop is empty, please check YEventloop" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YEventloop is empty");
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yEventloop->m_eventloopName);
    if(itEventLoop != m_eventLoop.end())
    {
        itEventLoop->second->start();
        return YomkResponse(YomkResponse::eOk, "event loop start success");
    }

    EventLoopPtr eventLoop = std::make_shared<EventLoop>();
    eventLoop->setDefaultEventHandleFinishedFunc(yEventloop->m_defaultEventHandleFinishedFunc);
    eventLoop->setDefaultServiceFunc(yEventloop->m_defaultServiceFunc);
    m_eventLoop[yEventloop->m_eventloopName] = eventLoop;
    eventLoop->start();

    return YomkResponse(YomkResponse::eOk, "event loop start success");
}

YomkResponse YomkEventLoop::stop(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YString is empty");
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yStr->d);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->stop();

    return YomkResponse(YomkResponse::eOk, "event loop stop success");
}

YomkResponse YomkEventLoop::post(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YomkEvent", YomkEvent, yEvent);
    if(!yEvent)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkEvent is empty, please check YomkEvent" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YomkEvent is empty");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yEvent->m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->post(yEvent);

    return YomkResponse(YomkResponse::eOk, "event loop post success");
}

YomkResponse YomkEventLoop::postWait(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YomkEvent", YomkEvent, yEvent);
    if(!yEvent)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkEvent is empty, please check YomkEvent" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YomkEvent is empty");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yEvent->m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->postWait(yEvent);

    return YomkResponse(YomkResponse::eOk, "event loop post wait success", yEvent);
}

YomkResponse YomkEventLoop::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return YomkResponse(YomkResponse::eErr, "YString is empty");
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yStr->d);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }

    itEventLoop->second->stop();
    m_eventLoop.erase(itEventLoop);
    
    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}
