#include "YomkEventLoop.h"

#include <iostream>
#include <stdexcept>
#include <vector>

YomkEventLoop::YomkEventLoop(YomkServer* server) : YomkService(server)
{
    name("/YomkEventLoop");
}

int YomkEventLoop::init()
{
    installFunc("/start", &YomkEventLoop::start, "Eventloop");
    installFunc("/stop", &YomkEventLoop::stop, "String");
    installFunc("/post", &YomkEventLoop::post, "Event");
    installFunc("/post_wait", &YomkEventLoop::postWait, "Event");
    installFunc("/destroy", &YomkEventLoop::destroy, "String");
    // 调试内省接口，端点挂在本服务 funcMap
    installFunc("/loops", &YomkEventLoop::loops);
    installFunc("/loop", &YomkEventLoop::loopInfo, "String");
    installFunc("/all", &YomkEventLoop::listAll);
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
    // 内层返回码映射：0 成功；其余（实际仅 2=循环未运行被拒，事件为空不可达——API 宏总构造有效事件）统一 eNo
    int rc = itEventLoop->second->post(event);
    if (rc != 0)
    {
        return YomkResponse(YomkResponse::eNo, "event loop not running, post rejected");
    }

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
    // 内层返回码映射：0 成功（回传 event 携带执行结果）；其余统一 eNo（未入队未执行，不回传）
    int rc = itEventLoop->second->postWait(event);
    if (rc != 0)
    {
        return YomkResponse(YomkResponse::eNo, "event loop not running, post wait rejected");
    }

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
        for (auto& item : m_eventLoop)
        {
            loopNames.push_back(item.first);
        }
    }
    return {YomkResponse::eOk, "ok", yomk::StringArray_::shared(loopNames)};
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
            // 纯数字但超过无符号范围时 stoul 抛 out_of_range（invalid_argument 不可达：
            // 纯数字且非空已过滤），捕获回退默认 tagCount=3，避免异常穿透同步调用方崩溃
            try
            {
                tagCount = static_cast<size_t>(std::stoul(countStr));
            }
            catch (const std::out_of_range&)
            {
                tagCount = 3;
            }
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

    // 命中：结构化快照，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）
    yomk::EventLoopInfo info;
    info.m_loopName = loopName;
    eventLoop->infoSnapshot(info, tagCount);
    return {YomkResponse::eOk, "ok", yomk::EventLoopInfo_::shared(info)};
}

// 内省：全部事件循环元信息，锁内拷贝快照、锁外逐循环取结构化快照（m_msg 恒 "ok"，数据走 m_data）
YomkResponse YomkEventLoop::listAll(YomkPkgPtr pkg)
{
    std::vector<EventLoopPtr> eventLoops;
    std::vector<std::string> loopNames;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        for (auto& item : m_eventLoop)
        {
            loopNames.push_back(item.first);
            eventLoops.push_back(item.second);
        }
    }

    std::vector<yomk::EventLoopInfo> infos;
    infos.reserve(eventLoops.size());
    for (size_t i = 0; i < eventLoops.size(); ++i)
    {
        yomk::EventLoopInfo info;
        info.m_loopName = loopNames[i];
        eventLoops[i]->infoSnapshot(info, 3);
        infos.push_back(std::move(info));
    }
    return {YomkResponse::eOk, "ok", yomk::EventLoopInfoList_::shared(infos)};
}
