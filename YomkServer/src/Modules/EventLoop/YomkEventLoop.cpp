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
    YomkUnPackPkgResponse(pkg, Eventloop, eventloop);
    if(!eventloop)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "Eventloop is empty, please check Eventloop" << std::endl;
        return YomkResponse(YomkResponse::eErr, "Eventloop is empty");
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(eventloop->d.m_eventloopName);
    if(itEventLoop != m_eventLoop.end())
    {
        itEventLoop->second->start();
        return YomkResponse(YomkResponse::eOk, "event loop start success");
    }

    EventLoopPtr eventLoop = std::make_shared<EventLoop>();
    eventLoop->setDefaultServiceFunc(eventloop->d.m_defaultServiceFunc);
    m_eventLoop[eventloop->d.m_eventloopName] = eventLoop;
    eventLoop->start();

    return YomkResponse(YomkResponse::eOk, "event loop start success");
}

YomkResponse YomkEventLoop::stop(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "string is empty, please check string" << std::endl;
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop: " << str->d << " not exist, please check event loop name" << std::endl;
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
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop: " << yEvent->m_eventLoopName << " not exist, please check event loop name" << std::endl;
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
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop: " << yEvent->m_eventLoopName << " not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->postWait(yEvent);

    return YomkResponse(YomkResponse::eOk, "event loop post wait success", yEvent);
}

YomkResponse YomkEventLoop::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "string is empty, please check string" << std::endl;
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if(itEventLoop == m_eventLoop.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "event loop: " << str->d << " not exist, please check event loop name" << std::endl;
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }

    itEventLoop->second->stop();
    m_eventLoop.erase(itEventLoop);
    
    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}
