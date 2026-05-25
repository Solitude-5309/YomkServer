#include "EventLoop.h"
#include <iostream>

EventLoop::EventLoop()
    : m_running(false)
    , m_eventId(1)
{
    
}

EventLoop::~EventLoop()
{
    if(m_running.load())
    {
        stop();
    }
}

int EventLoop::start()
{
    if(m_running.load())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop already running, please do not start it again" << std::endl;
        return 0;
    }

    m_running.store(true);
    m_worker = std::thread([this]()
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
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop: event is null, please check event" << std::endl;
                continue;
            }

            event->d.handle();
            if(event->d.m_waitCallback)
            {
                event->d.m_waitCallback();
            }
        }
    });
    return 0;
}

int EventLoop::stop()
{
    if(!m_running.load())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop not running, please do not stop it again" << std::endl;
        return 0;
    }
    m_running.store(false);
    m_condition.notify_one();
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
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop not running, please start event loop." << std::endl;
        return 0;
    }

    if(!event)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop: event is null, please check event" << std::endl;
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
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop not running, please start event loop." << std::endl;
        return 0;
    }
    
    if(!event)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop: event is null, please check event" << std::endl;
        return 1;
    }

    if(std::this_thread::get_id() == m_worker.get_id())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve deadlock" << std::endl;
        event->d.handle();
        return 0;
    }

    std::condition_variable tmpCv;
    std::mutex tmpMtx;
    std::unique_lock<std::mutex> lock(tmpMtx);

    event->d.m_waitCallback = [&tmpCv]()
    {
        tmpCv.notify_all();
    };

    post(event);

    tmpCv.wait(lock);

    return 0;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc)
{
    m_defaultServiceFunc = serviceFunc;
}
