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
    yomk::Response start(YomkPkgPtr pkg);
    yomk::Response stop(YomkPkgPtr pkg);
    yomk::Response post(YomkPkgPtr pkg);
    yomk::Response postWait(YomkPkgPtr pkg);
    yomk::Response destroy(YomkPkgPtr pkg);
    yomk::Response loops(YomkPkgPtr pkg);
    yomk::Response loopInfo(YomkPkgPtr pkg);
    yomk::Response listAll(YomkPkgPtr pkg);

private:
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};