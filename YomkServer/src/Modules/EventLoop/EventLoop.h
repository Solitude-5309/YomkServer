#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <string>
#include "YomkPkg.h"

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();

public:
    int start();
    int stop();    // 停止：仅退出工作线程、不清空队列——未执行事件保留待下次 start 续跑
    int destroy(); // 销毁：先停止退出工作线程，再清空未执行的排队事件（不可续跑）
    int post(YomkPtr(Event) event);
    int postWait(YomkPtr(Event) event);
    void setDefaultServiceFunc(YomkServiceFunc serviceFunc, const std::string &msgName);
    std::string infoLine(const std::string &loopName, size_t tagCount);
    void run();

private:
    std::queue<YomkPtr(Event)> m_eventQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    std::atomic<bool> m_running;
    std::uint64_t m_eventId;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_defaultMsgName; // 默认处理函数期望的消息类型名（仅内省元数据，可为空）
};
typedef std::shared_ptr<EventLoop> EventLoopPtr;