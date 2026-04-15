#include "EventLoop.h"
#include <iostream>

EventLoop::EventLoop()
    : m_running(false)
    , m_eventId(0)
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
            YomkEventPtr event;
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

            event->handle();
            event->m_eventHandleFinished = true;
            event->handleFinished(event);
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

int EventLoop::post(YomkEventPtr event)
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
        event->m_eventId = ++m_eventId;
        if(!event->m_eventHandleFinishedFunc)
        {
            event->m_eventHandleFinishedFunc = m_defaultEventHandleFinishedFunc;
        }
        if(!event->m_serviceFunc)
        {
            event->m_serviceFunc = m_defaultServiceFunc;
        }
        m_eventQueue.push(event);
    }
    m_condition.notify_one();
    return 0;
}

int EventLoop::postWait(YomkEventPtr event)
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

    if(!event->m_eventHandleFinishedFunc)
    {
        event->m_eventHandleFinishedFunc = m_defaultEventHandleFinishedFunc;
    }
    if(!event->m_serviceFunc)
    {
        event->m_serviceFunc = m_defaultServiceFunc;
    }

    if(std::this_thread::get_id() == m_worker.get_id())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve deadlock" << std::endl;
        event->handle();
        event->m_eventHandleFinished = true;
        event->handleFinished(event);
        return 0;
    }

    std::condition_variable tmpCv;
    std::mutex tmpMtx;
    std::unique_lock<std::mutex> lock(tmpMtx);

    std::function<void(std::shared_ptr<YomkEvent> eventPtr)> tmpEventHandleFinishedFunc = event->m_eventHandleFinishedFunc;

    event->m_eventHandleFinishedFunc = [&tmpCv, &tmpEventHandleFinishedFunc](std::shared_ptr<YomkEvent> eventPtr){
        if(tmpEventHandleFinishedFunc)
        {
            tmpEventHandleFinishedFunc(eventPtr);
        }
        tmpCv.notify_one();
    };

    post(event);

    tmpCv.wait(lock, [this, &event]()
    {
        return 0;
    });

    return 0;
}

void EventLoop::setDefaultEventHandleFinishedFunc(std::function<void(std::shared_ptr<YomkEvent> eventPtr)> eventHandleFinishedFunc)
{
    m_defaultEventHandleFinishedFunc = eventHandleFinishedFunc;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc)
{
    m_defaultServiceFunc = serviceFunc;
}
