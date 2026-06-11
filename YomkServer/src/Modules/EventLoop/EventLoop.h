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
    void run();
private:
    std::queue<YomkPtr(Event)> m_eventQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    std::atomic<bool> m_running;
    std::uint64_t m_eventId;
    YomkServiceFunc m_defaultServiceFunc;
};
typedef std::shared_ptr<EventLoop> EventLoopPtr;