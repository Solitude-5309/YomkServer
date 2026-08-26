#include "EventLoop.h"
#include <iostream>
#include <vector>
#include "YomkDefine.h"

EventLoop::EventLoop()
    : m_running(false), m_eventId(1)
{
}

EventLoop::~EventLoop()
{
    if (m_running.exchange(false))
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            while (!m_eventQueue.empty())
            {
                m_eventQueue.pop();
            }
        }
        m_condition.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }
}

int EventLoop::start()
{
    if (m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop already running, please do not start it again");
        return 0;
    }
    m_running.store(true);
    m_worker = std::thread(std::bind(&EventLoop::run, this));
    return 0;
}

int EventLoop::stop()
{
    if (!m_running.load())
    {
        return 0;
    }
    m_running.store(false);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_eventQueue.empty())
        {
            m_eventQueue.pop();
        }
    }

    m_condition.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    return 0;
}

int EventLoop::post(YomkPtr(Event) event)
{
    if (!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please start event loop.");
        return 0;
    }

    if (!event)
    {
        YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        event->d.m_eventId = ++m_eventId;
        if (!event->d.m_serviceFunc)
        {
            event->d.m_serviceFunc = m_defaultServiceFunc;
        }
        m_eventQueue.push(event);
    }
    m_condition.notify_one();
    return 0;
}

int EventLoop::postWait(YomkPtr(Event) event)
{
    if (!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please start event loop.");
        return 0;
    }

    if (!event)
    {
        YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
        return 1;
    }

    if (std::this_thread::get_id() == m_worker.get_id())
    {
        YOMK_ERR_POS_LOG("EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve deadlock");
        event->d.handle();
        return 0;
    }

    std::condition_variable tmpCv;
    std::mutex tmpMtx;
    bool notified = false;
    std::unique_lock<std::mutex> lock(tmpMtx);

    event->d.m_waitCallback = [&tmpCv, &notified, &tmpMtx]()
    {
        {
            std::lock_guard<std::mutex> lk(tmpMtx);
            notified = true;
        }
        tmpCv.notify_all();
    };

    post(event);

    tmpCv.wait(lock, [&notified]()
               { return notified; });

    return 0;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc)
{
    m_defaultServiceFunc = serviceFunc;
}

// 内省元信息行：name running:on|off pending:N defaultFunc:on|off nextNEventTag(N): tag1, tag2, ...
// 队首最多列出 tagCount 个事件 tag，队列不足则全部列出，空 tag 显示 - 占位
std::string EventLoop::infoLine(const std::string &loopName, size_t tagCount)
{
    bool running = false;
    size_t pending = 0;
    bool defaultFunc = false;
    std::vector<std::string> tags;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        running = m_running.load();
        pending = m_eventQueue.size();
        defaultFunc = static_cast<bool>(m_defaultServiceFunc);
        std::queue<YomkPtr(Event)> queueCopy = m_eventQueue;
        while (!queueCopy.empty() && tags.size() < tagCount)
        {
            auto event = queueCopy.front();
            queueCopy.pop();
            tags.push_back(event && !event->d.m_tag.empty() ? event->d.m_tag : "-");
        }
    }

    std::string tagList;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        if (i > 0)
        {
            tagList += ", ";
        }
        tagList += tags[i];
    }

    return loopName +
           (running ? " running:on" : " running:off") +
           " pending:" + std::to_string(pending) +
           (defaultFunc ? " defaultFunc:on" : " defaultFunc:off") +
           " nextNEventTag(" + std::to_string(tagCount) + "): " + tagList;
}

void EventLoop::run()
{
    while (m_running.load())
    {
        YomkPtr(Event) event;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this]()
                             { return !m_eventQueue.empty() || !m_running.load(); });
            if (!m_running.load())
            {
                break;
            }
            event = m_eventQueue.front();
            m_eventQueue.pop();
        }
        if (!event)
        {
            YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
            continue;
        }

        try
        {
            event->d.handle();
        }
        catch (const std::exception &e)
        {
            YOMK_ERR_POS_LOG("EventLoop: " + event->d.m_eventLoopName + " exec event id: " + std::to_string(event->d.m_eventId) + " caught, what: " + std::string(e.what()));
        }
        catch (...)
        {
            YOMK_ERR_POS_LOG("EventLoop: unknown exception caught");
        }

        if (event->d.m_waitCallback)
        {
            event->d.m_waitCallback();
        }
    }
}
