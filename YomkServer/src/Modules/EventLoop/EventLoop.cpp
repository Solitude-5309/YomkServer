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
        std::cout << "EventLoop already running" << std::endl;
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
                std::cout << "EventLoop: event is null" << std::endl;
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
        std::cout << "EventLoop not running" << std::endl;
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
        std::cout << "EventLoop not running" << std::endl;
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        event->m_eventId = ++m_eventId;
        m_eventQueue.push(event);
    }
    m_condition.notify_one();
    return 0;
}

int EventLoop::postWait(YomkEventPtr event)
{
    if(!m_running.load())
    {
        std::cout << "EventLoop not running" << std::endl;
        return 0;
    }

    if(std::this_thread::get_id() == m_worker.get_id())
    {
        std::cout << "EventLoop deadlock: post wait in worker thread, is not allowed" << std::endl;
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
        return event->m_eventHandleFinished;
    });

    return 0;
}
