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
    // 持队列锁创建并赋值线程句柄：postWait 的自 join 防护读取 m_worker.get_id()，
    // 与本处 move-assign 若无互斥则构成对 std::thread 对象的并发读写（UB）；
    // 新 worker 的 run() 入口需先等锁释放，持锁创建无死锁
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_worker = std::thread(std::bind(&EventLoop::run, this));
    }
    return 0;
}

// 停止：仅退出工作线程，不清空队列——未执行事件保留，待下次 start 续跑（事件不丢失）；
// notify 必须持队列锁：若在锁外，worker 可能持锁评估完谓词（读到 running=true）但尚未入睡，
// 本线程的 store+notify 从旁边穿过——通知落在等待开始前不被 pending，worker 睡死（丢失唤醒）；
// 持锁后 notify 与谓词检查互斥串行，两个时序方向均安全
int EventLoop::stop()
{
    if (!m_running.load())
    {
        return 0;
    }
    m_running.store(false);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_condition.notify_all();
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    return 0;
}

// 销毁：先停止退出工作线程，再清空未执行的排队事件（不可续跑）；
// 清空前逐个触发被丢弃事件的等待回调，释放 postWait 等待者（否则丢弃后其永久挂起）；
// 回调在锁外调用（与 run() 一致）——若持 m_queueMutex 调回调（内部锁 postWait 的 tmpMtx），
// 与 postWait 持 tmpMtx 调 post()（内部锁 m_queueMutex）构成 ABBA 死锁；
// 停止态下调用幂等（stop 早退 + 空队列清空为无害空操作），析构与显式销毁可重复进入
int EventLoop::destroy()
{
    stop();
    std::vector<YomkPtr(Event)> discarded;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_eventQueue.empty())
        {
            discarded.push_back(m_eventQueue.front());
            m_eventQueue.pop();
        }
    }
    for (auto &event : discarded)
    {
        if (event && event->d.m_waitCallback)
        {
            event->d.m_waitCallback();
        }
    }
    return 0;
}

int EventLoop::post(YomkPtr(Event) event)
{
    if (!m_running.load())
    {
        YOMK_ERR_POS_LOG("EventLoop not running, please start event loop.");
        return 2; // 循环未运行投递被拒，与入队成功(0)/事件为空(1)区分，供上层映射错误码
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
        return 2; // 循环未运行投递被拒，与入队成功(0)/事件为空(1)区分
    }

    if (!event)
    {
        YOMK_ERR_POS_LOG("EventLoop: event is null, please check event");
        return 1;
    }

    // 自 join 防护读取持队列锁：与 start() 持锁 move-assign m_worker 互斥串行，避免无锁并发读写
    bool inWorkerThread = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        inWorkerThread = (std::this_thread::get_id() == m_worker.get_id());
    }
    if (inWorkerThread)
    {
        YOMK_ERR_POS_LOG("EventLoop deadlock: post wait in worker thread, is not allowed, directly execute current event to resolve deadlock");
        event->d.handle();
        return 0;
    }

    std::condition_variable tmpCv;
    std::mutex tmpMtx;
    bool notified = false;
    // 误报豁免：tmpMtx 同时被 m_waitCallback 内的 lock_guard 跨线程加锁（lambda 引用捕获，工作线程执行，
    // cppcheck 同域启发式未跟踪），承担 wait 谓词同步与 destroy-vs-notify 防护，非"同域无效锁"
    // cppcheck-suppress localMutex
    std::unique_lock<std::mutex> lock(tmpMtx);

    // notify 在持锁内进行：若在锁外 notify，等待方从 wait 醒来（notified=true）后析构 tmpCv，
    // 与本线程锁外的 notify_all 无 happens-before 边，构成 destroy-vs-notify 竞态（POSIX UB，TSan 如实报告）
    event->d.m_waitCallback = [&tmpCv, &notified, &tmpMtx]()
    {
        std::lock_guard<std::mutex> lk(tmpMtx);
        notified = true;
        tmpCv.notify_all();
    };

    // 仅在实际入队成功后才等待：检查与入队之间循环被停止/销毁时 post 返回非零，
    // 直接返回避免等待一个永不被执行/触发的回调（TOCTOU 分支）
    int rc = post(event);
    if (rc != 0)
    {
        return rc;
    }

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
