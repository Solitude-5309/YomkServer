#include "YomkEventLoop.h"
#include <iostream>
#include <vector>

YomkEventLoop::YomkEventLoop(YomkServer *server)
    : YomkService(server)
{
    name("/YomkEventLoop");
}

int YomkEventLoop::init()
{
    YomkInstallFunc("/start", YomkEventLoop::start, Eventloop);
    YomkInstallFunc("/stop", YomkEventLoop::stop, String);
    YomkInstallFunc("/post", YomkEventLoop::post, Event);
    YomkInstallFunc("/post_wait", YomkEventLoop::postWait, Event);
    YomkInstallFunc("/destroy", YomkEventLoop::destroy, String);
    // 调试内省接口，端点挂在本服务 funcMap
    YomkInstallFunc("/loops", YomkEventLoop::loops);
    YomkInstallFunc("/loop", YomkEventLoop::loopInfo, String);
    YomkInstallFunc("/all", YomkEventLoop::listAll);
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
    eventLoop->setDefaultServiceFunc(eventloop->d.m_defaultServiceFunc, eventloop->d.m_msgName);
    eventLoop->start();
    m_eventLoop.emplace(eventloop->d.m_eventloopName, eventLoop);

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
        return YomkResponse(YomkResponse::eNo, "event loop not exist");
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
        return YomkResponse(YomkResponse::eNo, "event loop not exist");
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
        return YomkResponse(YomkResponse::eNo, "event loop not exist");
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
        return YomkResponse(YomkResponse::eNo, "event loop not exist");
    }

    itEventLoop->second->destroy();
    m_eventLoop.erase(itEventLoop);

    return YomkResponse(YomkResponse::eOk, "event loop destroy success");
}

// 内省：列出全部事件循环名
YomkResponse YomkEventLoop::loops(YomkPkgPtr pkg)
{
    std::vector<std::string> loopNames;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        for (auto &item : m_eventLoop)
        {
            loopNames.push_back(item.first);
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, loopNames)};
}

// 内省：单个事件循环元信息，入参 String 格式：循环名 或 循环名 N（N 缺省 3）
YomkResponse YomkEventLoop::loopInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    std::string input = str->d;
    std::string loopName = input;
    size_t tagCount = 3;
    size_t spacePos = input.rfind(' ');
    if (spacePos != std::string::npos)
    {
        std::string countStr = input.substr(spacePos + 1);
        if (!countStr.empty() && countStr.find_first_not_of("0123456789") == std::string::npos)
        {
            loopName = input.substr(0, spacePos);
            tagCount = static_cast<size_t>(std::stoul(countStr));
        }
    }

    EventLoopPtr eventLoop;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        auto itEventLoop = m_eventLoop.find(loopName);
        if (itEventLoop == m_eventLoop.end())
        {
            YOMK_ERR_POS_LOG("event loop: " + loopName + " not exist, please check event loop name");
            return YomkResponse(YomkResponse::eNo, "event loop not exist");
        }
        eventLoop = itEventLoop->second;
    }

    return YomkResponse(YomkResponse::eOk, eventLoop->infoLine(loopName, tagCount));
}

// 内省：全部事件循环元信息，每行一个循环
YomkResponse YomkEventLoop::listAll(YomkPkgPtr pkg)
{
    std::vector<EventLoopPtr> eventLoops;
    std::vector<std::string> loopNames;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        for (auto &item : m_eventLoop)
        {
            loopNames.push_back(item.first);
            eventLoops.push_back(item.second);
        }
    }

    std::vector<std::string> lines;
    for (size_t i = 0; i < eventLoops.size(); ++i)
    {
        lines.push_back(eventLoops[i]->infoLine(loopNames[i], 3));
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}
