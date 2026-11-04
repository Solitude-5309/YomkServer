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
    yomk::payload::Response start(YomkPkgPtr pkg);
    yomk::payload::Response stop(YomkPkgPtr pkg);
    yomk::payload::Response post(YomkPkgPtr pkg);
    yomk::payload::Response postWait(YomkPkgPtr pkg);
    yomk::payload::Response destroy(YomkPkgPtr pkg);
    yomk::payload::Response loops(YomkPkgPtr pkg);
    yomk::payload::Response loopInfo(YomkPkgPtr pkg);
    yomk::payload::Response listAll(YomkPkgPtr pkg);

private:
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};