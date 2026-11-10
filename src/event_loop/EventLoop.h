#pragma once
#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>

#include "yomk/event_loop/YomkEventLoopMsg.h"
#include "yomk/event_loop/YomkEventLoopPayload.h"

class EventLoop
{
public:
    // loop 名随构造固化,供诊断文案携带具体循环名;默认名仅供白盒测试等无名场景,生产路径恒传具体名
    explicit EventLoop(const std::string& loopName = "unnamed");
    ~EventLoop();

public:
    int start();
    int stop();     // 停止:仅退出工作线程,不清空队列——未执行事件保留待下次 start 续跑
    int destroy();  // 销毁:先停止退出工作线程,再清空未执行事件——清空前释放其 postWait 等待者(不可续跑)
    int post(yomk::msgs::EventPtr event);      // 返回:0 入队成功 / 1 事件为空 / 2 循环未运行投递被拒
    int postWait(yomk::msgs::EventPtr event);  // 返回码同 post;同步等待执行完成(含异常场景)后返回
    void setDefaultServiceFunc(YomkServiceFunc serviceFunc, const std::string& msgName);
    // 结构化内省快照:填充事件循环元信息(getEventLoopInfo 返回数据);tag 存原值,空串保留
    void infoSnapshot(yomk::payload::EventLoopInfo& info, size_t tagCount);
    void run();

private:
    std::string m_loopName;  // 循环名(仅诊断文案内省用)
    std::queue<yomk::msgs::EventPtr> m_eventQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    std::atomic<bool> m_running;
    std::uint64_t m_eventId;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_defaultMsgName;  // 默认处理函数期望的消息类型名(仅内省元数据,可为空)
};
typedef std::shared_ptr<EventLoop> EventLoopPtr;