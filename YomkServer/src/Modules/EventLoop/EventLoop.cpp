#include "EventLoop.h"
#include <iostream>
#include "YomkDefine.h"

EventLoop::EventLoop()
    : m_running(false)
    , m_eventId(1)
{
    
}

EventLoop::~EventLoop()
{
    if(m_running.exchange(false))
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            while(!m_eventQueue.empty())
            {
                m_eventQueue.pop();
            }
        }
        m_condition.notify_all();
        if(m_worker.joinable())
        {
            m_worker.join();
        }
    }
}

int EventLoop::start()
{
    if(m_running.load())
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
    if(!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please do not stop it again");
        return 0;
    }
    m_running.store(false);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while(!m_eventQueue.empty())
        {
            m_eventQueue.pop();
        }
    }

    m_condition.notify_all();
    if(m_worker.joinable())
    {
        m_worker.join();
    }
    return 0;
}

int EventLoop::post(YomkPtr(Event) event)
{
    if(!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please start event loop.");
        return 0;
    }

    if(!event)
    {
        YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        event->d.m_eventId = ++m_eventId;
        if(!event->d.m_serviceFunc)
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
    if(!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please start event loop.");
        return 0;
    }
    
    if(!event)
    {
        YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
        return 1;
    }

    if(std::this_thread::get_id() == m_worker.get_id())
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

    tmpCv.wait(lock, [&notified]() { return notified; });

    return 0;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc)
{
    m_defaultServiceFunc = serviceFunc;
}

void EventLoop::run()
{
    while(m_running.load())
    {
        YomkPtr(Event) event;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this]()
            {
                return !m_eventQueue.empty() || !m_running.load();
            });
            if(!m_running.load())
            {
                break;
            }
            event = m_eventQueue.front();
            m_eventQueue.pop();
        }
        if(!event)
        {
            YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
            continue;
        }

        try 
        {
            event->d.handle();
        } 
        catch (const std::exception& e) 
        {
            YOMK_ERR_POS_LOG("EventLoop: " + event->d.m_eventLoopName + " exec event id: " + std::to_string(event->d.m_eventId) + " caught, what: " + std::string(e.what()));
        }
        catch (...)
        {
            YOMK_ERR_POS_LOG("EventLoop: unknown exception caught");
        }
        
        if(event->d.m_waitCallback)
        {
            event->d.m_waitCallback();
        }
    }
}
