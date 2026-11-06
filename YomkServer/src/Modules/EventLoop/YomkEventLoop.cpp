#include "YomkEventLoop.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include "YomkDiag.h"

YomkEventLoop::YomkEventLoop(YomkServer* server) : YomkService(server)
{
    name("/YomkEventLoop");
}

int YomkEventLoop::init()
{
    installFunc("/start", &YomkEventLoop::start, "EventLoop");
    installFunc("/stop", &YomkEventLoop::stop, "String");
    installFunc("/post", &YomkEventLoop::post, "Event");
    installFunc("/post_wait", &YomkEventLoop::postWait, "Event");
    installFunc("/destroy", &YomkEventLoop::destroy, "String");
    // 调试内省接口，端点挂在本服务 funcMap
    installFunc("/loops", &YomkEventLoop::loops);
    installFunc("/loop", &YomkEventLoop::loopInfo, "EventLoopQuery");
    installFunc("/all", &YomkEventLoop::listAll);
    return 0;
}

yomk::payload::Response YomkEventLoop::start(YomkMsgPtr pkg)
{
    auto eventloop = yomk::msgs::unpack<yomk::msgs::EventLoop>(pkg);
    if (!eventloop)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not EventLoop"};
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(eventloop->d.m_eventLoopName);
    if (itEventLoop != m_eventLoop.end())
    {
        itEventLoop->second->start();
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop start success");
    }

    EventLoopPtr eventLoop = std::make_shared<EventLoop>();
    eventLoop->setDefaultServiceFunc(eventloop->d.m_defaultServiceFunc, eventloop->d.m_msgName);
    eventLoop->start();
    m_eventLoop.emplace(eventloop->d.m_eventLoopName, eventLoop);

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop start success");
}

yomk::payload::Response YomkEventLoop::stop(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_DIAG_ERROR("event loop: " + str->d + " not exist, please check event loop name");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not exist");
    }
    itEventLoop->second->stop();

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop stop success");
}

yomk::payload::Response YomkEventLoop::post(YomkMsgPtr pkg)
{
    auto event = yomk::msgs::unpack<yomk::msgs::Event>(pkg);
    if (!event)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Event"};
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_DIAG_ERROR("event loop: " + event->d.m_eventLoopName + " not exist, please check event loop name");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not exist");
    }
    // 内层返回码映射：0 成功；其余（实际仅 2=循环未运行被拒，事件为空不可达——API 宏总构造有效事件）统一 ResStatus::Failed
    int rc = itEventLoop->second->post(event);
    if (rc != 0)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not running, post rejected");
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop post success");
}

yomk::payload::Response YomkEventLoop::postWait(YomkMsgPtr pkg)
{
    auto event = yomk::msgs::unpack<yomk::msgs::Event>(pkg);
    if (!event)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not Event"};
    }
    std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(event->d.m_eventLoopName);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_DIAG_ERROR("event loop: " + event->d.m_eventLoopName + " not exist, please check event loop name");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not exist");
    }
    // 内层返回码映射：0 成功（回传 event 携带执行结果）；其余统一 ResStatus::Failed（未入队未执行，不回传）
    int rc = itEventLoop->second->postWait(event);
    if (rc != 0)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not running, post wait rejected");
    }

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop post wait success", event);
}

yomk::payload::Response YomkEventLoop::destroy(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::unique_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
    auto itEventLoop = m_eventLoop.find(str->d);
    if (itEventLoop == m_eventLoop.end())
    {
        YOMK_DIAG_ERROR("event loop: " + str->d + " not exist, please check event loop name");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not exist");
    }

    itEventLoop->second->destroy();
    m_eventLoop.erase(itEventLoop);

    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "event loop destroy success");
}

// 内省：列出全部事件循环名
yomk::payload::Response YomkEventLoop::loops(YomkMsgPtr pkg)
{
    std::vector<std::string> loopNames;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        for (auto& item : m_eventLoop)
        {
            loopNames.push_back(item.first);
        }
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(loopNames)};
}

// 内省：单个事件循环元信息，入参 String 格式：循环名 或 循环名 N（N 缺省 3）
yomk::payload::Response YomkEventLoop::loopInfo(YomkMsgPtr pkg)
{
    auto query = yomk::msgs::unpack<yomk::msgs::EventLoopQuery>(pkg);
    if (!query)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not EventLoopQuery"};
    }
    // 名称按字面量精确匹配、采样数为类型化字段（历史 String "name N" 空格编码已移除）
    const std::string& loopName = query->d.m_loopName;
    size_t tagCount = query->d.m_tagCount;

    EventLoopPtr eventLoop;
    {
        std::shared_lock<std::shared_mutex> lockEventLoop(m_eventLoopMutex);
        auto itEventLoop = m_eventLoop.find(loopName);
        if (itEventLoop == m_eventLoop.end())
        {
            YOMK_DIAG_ERROR("event loop: " + loopName + " not exist, please check event loop name");
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "event loop not exist");
        }
        eventLoop = itEventLoop->second;
    }

    // 命中：结构化快照，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）
    yomk::payload::EventLoopInfo info;
    info.m_loopName = loopName;
    eventLoop->infoSnapshot(info, tagCount);
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::EventLoopInfo::shared(info)};
}

// 内省：全部事件循环元信息，锁内拷贝快照、锁外逐循环取结构化快照（m_msg 恒 "ok"，数据走 m_data）
yomk::payload::Response YomkEventLoop::listAll(YomkMsgPtr pkg)
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

    std::vector<yomk::payload::EventLoopInfo> infos;
    infos.reserve(eventLoops.size());
    for (size_t i = 0; i < eventLoops.size(); ++i)
    {
        yomk::payload::EventLoopInfo info;
        info.m_loopName = loopNames[i];
        eventLoops[i]->infoSnapshot(info, 3);
        infos.push_back(std::move(info));
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::EventLoopInfoList::shared(infos)};
}
