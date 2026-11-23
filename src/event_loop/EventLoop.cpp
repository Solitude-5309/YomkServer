#include "EventLoop.h"
#include "yomk/event_loop/YomkEventLoopMsg.h"

#include <iostream>
#include <vector>

#include "yomk/YomkDiag.h"

EventLoop::EventLoop(const std::string& loopName) : m_running(false), m_eventId(1), m_loopName(loopName) {}

EventLoop::~EventLoop()
{
    destroy();
}

int EventLoop::start()
{
    if (m_running.load())
    {
        YOMK_DIAG_INFO("event loop already running, start is idempotent");
        return 0;
    }
    m_running.store(true);
    // 持队列锁创建并赋值线程句柄:postWait 的自 join 防护读取 m_worker.get_id(),
    // 与本处 move-assign 若无互斥则构成对 std::thread 对象的并发读写(UB);
    // 新 worker 的 run() 入口需先等锁释放,持锁创建无死锁
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_worker = std::thread(std::bind(&EventLoop::run, this));
    }
    return 0;
}

// 停止:仅退出工作线程,不清空队列——未执行事件保留,待下次 start 续跑(事件不丢失);
// notify 必须持队列锁:若在锁外,worker 可能持锁评估完谓词(读到 running=true)但尚未入睡,
// 本线程的 store+notify 从旁边穿过——通知落在等待开始前不被 pending,worker 睡死(丢失唤醒);
// 持锁后 notify 与谓词检查互斥串行,两个时序方向均安全
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

// 销毁:先停止退出工作线程,再清空未执行的排队事件(不可续跑);
// 清空前逐个以 Failed 终结被丢弃事件的完成回调,释放 postWait 等待者并让调用方拿到明确失败(而非"成功但无结果");
// 回调在锁外调用(与 run() 一致)——若持 m_queueMutex 调回调(内部可能锁 postWait 的等待槽),
// 与 postWait 持等待槽锁调 enqueue()(内部锁 m_queueMutex)构成 ABBA 死锁;
// 停止态下调用幂等(stop 早退 + 空队列清空为无害空操作),析构与显式销毁可重复进入
int EventLoop::destroy()
{
    stop();
    std::vector<EventPtr> discarded;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_eventQueue.empty())
        {
            discarded.push_back(m_eventQueue.front());
            m_eventQueue.pop();
        }
    }
    for (auto& event : discarded)
    {
        finish(event, {yomk::payload::ResStatus::Failed, "event loop destroyed, pending event discarded"});
    }
    return 0;
}

EventPtr EventLoop::makeEvent(const yomk::payload::EventOpts& opts) const
{
    EventPtr event = std::make_shared<Event>();
    event->loopName = opts.eventLoopName;
    event->msg = opts.msg;
    event->handler = opts.serviceFunc;
    event->tag = opts.tag;
    return event;
}

int EventLoop::enqueue(EventPtr event, std::uint64_t* outEventId)
{
    // 与 postWait 的"检查-入队"间隙对齐:期间循环被停止/销毁则拒绝,避免等待永不被终结的事件(TOCTOU)
    if (!m_running.load())
    {
        return 2;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        event->eventId = ++m_eventId;
        if (!event->handler)
        {
            event->handler = m_defaultServiceFunc;
        }
        m_eventQueue.push(event);
        if (outEventId)
        {
            *outEventId = event->eventId;
        }
        // 丢失唤醒防护(对齐 stop() 与 YomkSimpleThreadPool::stop 同款先例):notify 若在锁外,
        // worker 可能持锁评估完谓词(读到队空)但尚未入睡,本线程入队+通知从旁边穿过——
        // 通知落在等待开始前不被 pending,worker 睡到下一事件/stop 才醒(事件延迟交付;
        // 末事件睡死且不再投递时 postWait 等待者随事件一起滞留).入队持锁 + 通知持锁后,
        // 通知与谓词检查互斥串行,两个时序方向均安全
        m_condition.notify_one();
    }
    return 0;
}

int EventLoop::post(const yomk::payload::EventOpts& opts, std::uint64_t* outEventId)
{
    if (!m_running.load())
    {
        return 2;  // 循环未运行投递被拒,与入队成功(0)区分,供上层映射错误码
    }

    EventPtr event = makeEvent(opts);
    event->onDone = opts.onDone;  // 异步:搬运用户完成回调(可空=丢弃结果)
    return enqueue(std::move(event), outEventId);
}

int EventLoop::postWait(const yomk::payload::EventOpts& opts, yomk::payload::Response& outResponse,
                        std::uint64_t* outEventId)
{
    if (!m_running.load())
    {
        return 2;  // 循环未运行投递被拒
    }

    // 自 join 防护读取持队列锁:与 start() 持锁 move-assign m_worker 互斥串行,避免无锁并发读写
    bool inWorkerThread = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        inWorkerThread = (std::this_thread::get_id() == m_worker.get_id());
    }

    EventPtr event = makeEvent(opts);
    if (inWorkerThread)
    {
        YOMK_DIAG_ERROR(
            "EventLoop: " + m_loopName + " post wait in worker thread detected deadlock, directly execute current event "
            "to resolve deadlock");
        // 自 join 不等待:就地执行并把结果直接写入调用方变量;此时 onDone 为空,无需终结回调
        outResponse = execute(event);
        if (outEventId)
        {
            *outEventId = 0;
        }
        return 0;
    }

    // 同步结果槽:onDone 改绑内部闭包(不搬 opts.onDone),写栈上结果并唤醒等待者;
    // 捕获的引用在 wait 期间始终有效(事件必被 execute+finish 或 destroy 丢弃终结,两条路径都会调 onDone)
    struct WaitSlot
    {
        yomk::payload::Response response;
        bool done = false;
        std::mutex mtx;
        std::condition_variable cv;
    };
    // slot.mtx 同时被 onDone 闭包跨线程加锁(lambda 引用捕获,工作线程执行),
    // 承担 wait 谓词同步与 destroy-vs-notify 防护,非"同域无效锁"
    WaitSlot slot;
    // notify 在持锁内进行:若在锁外 notify,等待方从 wait 醒来(done=true)后析构 slot 的 cv,
    // 与本线程锁外的 notify_all 无 happens-before 边,构成 destroy-vs-notify 竞态(POSIX UB,TSan 如实报告)
    event->onDone = [&slot](yomk::payload::Response response)
    {
        std::lock_guard<std::mutex> lk(slot.mtx);
        slot.response = std::move(response);
        slot.done = true;
        slot.cv.notify_all();
    };

    // 仅在实际入队成功后才等待:检查与入队之间循环被停止/销毁时 enqueue 返回非零,
    // 直接返回避免等待一个永不被执行/终结的回调(TOCTOU 分支)
    int rc = enqueue(std::move(event), outEventId);
    if (rc != 0)
    {
        return rc;
    }

    std::unique_lock<std::mutex> lock(slot.mtx);
    slot.cv.wait(lock, [&slot]() { return slot.done; });
    outResponse = std::move(slot.response);
    return 0;
}

void EventLoop::setDefaultServiceFunc(YomkServiceFunc serviceFunc, const std::string& msgName)
{
    m_defaultServiceFunc = serviceFunc;
    m_defaultMsgName = msgName;
}

// 结构化内省快照:填充事件循环元信息(getEventLoopInfo 返回数据)
// m_queueMutex 锁内取数;队列头起最多 tagCount 条事件 tag,存原值(空串保留)
void EventLoop::infoSnapshot(yomk::payload::EventLoopInfo& info, size_t tagCount)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    info.running = m_running.load();
    info.pending = m_eventQueue.size();
    info.hasDefaultFunc = static_cast<bool>(m_defaultServiceFunc);
    info.msgName = m_defaultMsgName;
    std::queue<EventPtr> queueCopy = m_eventQueue;
    while (!queueCopy.empty() && info.nextTags.size() < tagCount)
    {
        auto event = queueCopy.front();
        queueCopy.pop();
        info.nextTags.push_back(event ? event->tag : "");
    }
}

yomk::payload::Response EventLoop::execute(const EventPtr& event) const
{
    if (!event || !event->handler)
    {
        return {yomk::payload::ResStatus::Failed, "event has no handler bound"};
    }
    try
    {
        return event->handler(event->msg);
    }
    catch (const std::exception& e)
    {
        YOMK_DIAG_ERROR(
            "EventLoop: " + event->loopName + " exec event id: " + std::to_string(event->eventId) + " caught, what: " +
            std::string(e.what()));
        return {yomk::payload::ResStatus::Failed, "event handler threw: " + std::string(e.what())};
    }
    catch (...)
    {
        YOMK_DIAG_ERROR("EventLoop: " + event->loopName + " exec event id: " + std::to_string(event->eventId) +
                        " caught an unknown exception");
        return {yomk::payload::ResStatus::Failed, "event handler threw an unknown exception"};
    }
}

void EventLoop::finish(const EventPtr& event, yomk::payload::Response result)
{
    if (!event || !event->onDone)
    {
        return;  // 无等待者/无观察者:结果丢弃(默认异步投递语义)
    }
    try
    {
        event->onDone(std::move(result));
    }
    catch (const std::exception& e)
    {
        YOMK_DIAG_ERROR("EventLoop: " + event->loopName + " onDone threw, what: " + std::string(e.what()));
    }
    catch (...)
    {
        YOMK_DIAG_ERROR("EventLoop: " + event->loopName + " onDone threw an unknown exception");
    }
}

void EventLoop::run()
{
    while (m_running.load())
    {
        EventPtr event;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this]() { return !m_eventQueue.empty() || !m_running.load(); });
            if (!m_running.load())
            {
                break;
            }
            event = m_eventQueue.front();
            m_eventQueue.pop();
        }
        if (!event)
        {
            continue;  // 事件为空不可达(队列只收 makeEvent 产物),防御守卫仅跳过
        }

        // 执行与终结分离:结果统一经 finish 交付,onDone 为空则丢弃,非空则回调一次
        auto result = execute(event);
        finish(event, std::move(result));
    }
}
