#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include "YomkPkg.h"

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();
public:
    int start();
    int stop();
    int post(YomkPtr(Event) event);
    int postWait(YomkPtr(Event) event);
    void setDefaultServiceFunc(YomkServiceFunc serviceFunc);
    void eventHandleFinished(uint64_t eventId);
private:
    std::queue<YomkPtr(Event)> m_eventQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    std::atomic<bool> m_running;
    std::uint64_t m_eventId;
    YomkServiceFunc m_defaultServiceFunc;
    std::condition_variable m_cv;
    std::mutex m_mtx;
    std::uint64_t m_curFinishedEventId;
};
typedef std::shared_ptr<EventLoop> EventLoopPtr;