#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <string>
#include <thread>

#include "Event.h"
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
    int destroy();  // 销毁:先停止退出工作线程,再清空未执行事件——清空前以 Failed 终结被丢弃事件的完成回调
    // 返回:0 入队成功 / 2 循环未运行投递被拒;outEventId 非空时回填入队赋号
    int post(const yomk::payload::EventOpts& opts, std::uint64_t* outEventId = nullptr);
    // 返回码同 post;同步等待执行完成(含异常/丢弃场景),执行结果写入 outResponse
    int postWait(const yomk::payload::EventOpts& opts, yomk::payload::Response& outResponse,
                 std::uint64_t* outEventId = nullptr);
    void setDefaultServiceFunc(YomkServiceFunc serviceFunc, const std::string& msgName);
    // 结构化内省快照:填充事件循环元信息(getEventLoopInfo 返回数据);tag 存原值,空串保留
    void infoSnapshot(yomk::payload::EventLoopInfo& info, size_t tagCount);
    void run();

private:
    // 构造内部执行单元:仅搬运用户输入,onDone 留空(由 post/postWait 各自占用该槽)
    EventPtr makeEvent(const yomk::payload::EventOpts& opts) const;
    // 入队:锁内赋 eventId、handler 为空时回落默认函数、入队并通知;循环已停止返回 2
    int enqueue(EventPtr event, std::uint64_t* outEventId);
    // 执行并把所有异常收敛为 Failed 结果(不向外抛),保证等待者/回调必被终结
    yomk::payload::Response execute(const EventPtr& event) const;
    // 完成终结:非空 onDone 时回调一次,异常只记日志(不得击穿 worker 循环)
    void finish(const EventPtr& event, yomk::payload::Response result);

    std::string m_loopName;  // 循环名(仅诊断文案内省用)
    std::queue<EventPtr> m_eventQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    std::atomic<bool> m_running;
    std::uint64_t m_eventId;
    YomkServiceFunc m_defaultServiceFunc;
    std::string m_defaultMsgName;  // 默认处理函数期望的消息类型名(仅内省元数据,可为空)
};
typedef std::shared_ptr<EventLoop> EventLoopPtr;
