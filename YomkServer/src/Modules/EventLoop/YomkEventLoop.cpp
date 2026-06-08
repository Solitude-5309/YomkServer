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
        YOMK_ERR_POS_LOG("Eventloop is empty, please check Eventloop");
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
        YOMK_ERR_POS_LOG("string is empty, please check string");
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if(itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + str->d + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->stop();

    return YomkResponse(YomkResponse::eOk, "event loop stop success");
}

YomkResponse YomkEventLoop::post(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Event, event);
    if(!event)
    {
        YOMK_ERR_POS_LOG("Event is empty, please check Event");
        return YomkResponse(YomkResponse::eErr, "Event is empty");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + event->d.m_eventLoopName + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->post(event);

    return YomkResponse(YomkResponse::eOk, "event loop post success");
}

YomkResponse YomkEventLoop::postWait(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Event, event);
    if(!event)
    {
        YOMK_ERR_POS_LOG("Event is empty, please check Event");
        return YomkResponse(YomkResponse::eErr, "Event is empty");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + event->d.m_eventLoopName + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->postWait(event);

    return YomkResponse(YomkResponse::eOk, "event loop post wait success", event);
}

YomkResponse YomkEventLoop::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        YOMK_ERR_POS_LOG("string is empty, please check string");
        return YomkResponse(YomkResponse::eErr, "string is empty");
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if(itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + str->d + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }

    itEventLoop->second->stop();
    m_eventLoop.erase(itEventLoop);
    
    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}
