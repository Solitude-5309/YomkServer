#include <map>
#include <mutex>
#include <shared_mutex>

#include "EventLoop.h"
#include "YomkExport.h"
#include "YomkServer.h"

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
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};