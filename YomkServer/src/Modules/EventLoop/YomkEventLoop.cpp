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

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yStr->d);
    if(itEventLoop == m_eventLoop.end())
    {
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->stop();

    return YomkResponse(YomkResponse::eOk, "event loop stop success");
}

YomkResponse YomkEventLoop::post(YomkPkgPtr pkg)
{
    YomkEventPtr eventPtr = std::dynamic_pointer_cast<YomkEvent>(pkg);
    if(!eventPtr)
    {
        return YomkResponse(YomkResponse::eErr, "post pkg is not event");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(eventPtr->m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->post(eventPtr);

    return YomkResponse(YomkResponse::eOk, "event loop post success");
}

YomkResponse YomkEventLoop::postWait(YomkPkgPtr pkg)
{
    YomkEventPtr eventPtr = std::dynamic_pointer_cast<YomkEvent>(pkg);
    if(!eventPtr)
    {
        return YomkResponse(YomkResponse::eErr, "post pkg is not event");
    }

    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(eventPtr->m_eventLoopName);
    if(itEventLoop == m_eventLoop.end())
    {
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->postWait(eventPtr);

    return YomkResponse(YomkResponse::eOk, "event loop post wait success", eventPtr);
}

YomkResponse YomkEventLoop::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);

    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(yStr->d);
    if(itEventLoop == m_eventLoop.end())
    {
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }

    itEventLoop->second->stop();
    m_eventLoop.erase(itEventLoop);
    
    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}
