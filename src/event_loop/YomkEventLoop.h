#include <map>
#include <mutex>
#include <shared_mutex>

#include "EventLoop.h"
#include "yomk/server/YomkServer.h"

class YomkEventLoop : public YomkService
{
public:
    YomkEventLoop(YomkServer* server);
    virtual ~YomkEventLoop() {}

public:
    virtual int init() override;

private:
    yomk::payload::Response start(YomkMsgPtr pkg);
    yomk::payload::Response stop(YomkMsgPtr pkg);
    yomk::payload::Response post(YomkMsgPtr pkg);
    yomk::payload::Response postWait(YomkMsgPtr pkg);
    yomk::payload::Response destroy(YomkMsgPtr pkg);
    yomk::payload::Response loops(YomkMsgPtr pkg);
    yomk::payload::Response loopInfo(YomkMsgPtr pkg);
    yomk::payload::Response listAll(YomkMsgPtr pkg);

private:
    // 不变量:表可收缩(destroy 移除条目),与其他模块"只增不减"表不同;
    // post/postWait/stop 路径须在 shared_lock 内拷出 EventLoopPtr 副本,锁外使用依赖副本存活
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};