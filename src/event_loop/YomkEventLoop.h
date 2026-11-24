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
    yomk::payload::Response start(YomkConstMsgPtr msg);
    yomk::payload::Response stop(YomkConstMsgPtr msg);
    yomk::payload::Response post(YomkConstMsgPtr msg);
    yomk::payload::Response postWait(YomkConstMsgPtr msg);
    yomk::payload::Response destroy(YomkConstMsgPtr msg);
    yomk::payload::Response loops(YomkConstMsgPtr msg);
    yomk::payload::Response loopInfo(YomkConstMsgPtr msg);
    yomk::payload::Response listAll(YomkConstMsgPtr msg);

private:
    // 不变量:表可收缩(destroy 移除条目),与其他模块"只增不减"表不同;
    // post/postWait/stop 路径须在 shared_lock 内拷出 EventLoopPtr 副本,锁外使用依赖副本存活
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};