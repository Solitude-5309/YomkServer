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
    destroy();
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

// 停止：仅退出工作线程，不清空队列——未执行事件保留，待下次 start 续跑（事件不丢失）；
// 不触碰 m_queueMutex，与 worker 锁外执行 handle() 无交叉、join 无锁交互
int EventLoop::stop()
{
    if (!m_running.load())
    {
        return 0;
    }
    m_running.store(false);

    m_condition.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    return 0;
}

// 销毁：先停止退出工作线程，再清空未执行的排队事件（不可续跑）；
// 停止态下调用幂等（stop 早退 + 空队列清空为无害空操作），析构与显式销毁可重复进入
int EventLoop::destroy()
{
    stop();
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_eventQueue.empty())
        {
            m_eventQueue.pop();
        }
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

    // notify 在持锁内进行：若在锁外 notify，等待方从 wait 醒来（notified=true）后析构 tmpCv，
    // 与本线程锁外的 notify_all 无 happens-before 边，构成 destroy-vs-notify 竞态（POSIX UB，TSan 如实报告）
    event->d.m_waitCallback = [&tmpCv, &notified, &tmpMtx]()
    {
        std::lock_guard<std::mutex> lk(tmpMtx);
        notified = true;
        tmpCv.notify_all();
    };

    post(event);

    tmpCv.wait(lock, [&notified]()
               { return notified; });

    return 0;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc, const std::string &msgName)
{
    m_defaultServiceFunc = serviceFunc;
    m_defaultMsgName = msgName;
}

// 内省元信息行：name running:on|off pending:N defaultFunc:on|off [类型名] nextNEventTag(N): tag1, tag2, ...
// 队首最多列出 tagCount 个事件 tag，队列不足则全部列出，空 tag 显示 - 占位；默认处理函数声明过类型时附加 [类型名]
std::string EventLoop::infoLine(const std::string &loopName, size_t tagCount)
{
    bool running = false;
    size_t pending = 0;
    bool defaultFunc = false;
    std::string defaultMsgName;
    std::vector<std::string> tags;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        running = m_running.load();
        pending = m_eventQueue.size();
        defaultFunc = static_cast<bool>(m_defaultServiceFunc);
        defaultMsgName = m_defaultMsgName;
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
           (defaultFunc && !defaultMsgName.empty() ? " [" + defaultMsgName + "]" : "") +
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
