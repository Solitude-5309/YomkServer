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
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(eventloop->d.m_eventloopName);
    if (itEventLoop != m_eventLoop.end())
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
    YomkUnPackPkgResponse(pkg, String, str);
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if (itEventLoop == m_eventLoop.end())
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
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if (itEventLoop == m_eventLoop.end())
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
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + event->d.m_eventLoopName + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }
    itEventLoop->second->postWait(event);

    return YomkResponse(YomkResponse::eOk, "event loop post wait success", event);
}

YomkResponse YomkEventLoop::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_ERR_POS_LOG("event loop: " + str->d + " not exist, please check event loop name");
        return YomkResponse(YomkResponse::eErr, "event loop not exist");
    }

    itEventLoop->second->stop();
    m_eventLoop.erase(itEventLoop);

    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}
