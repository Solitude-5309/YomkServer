#include <map>
#include <mutex>
#include <shared_mutex>

#include "EventLoop.h"
#include "YomkDefine.h"
#include "YomkServer.h"

class YomkEventLoop : public YomkService
{
public:
    YomkEventLoop(YomkServer* server);
    virtual ~YomkEventLoop() {}

public:
    virtual int init() override;

private:
    YomkResponse start(YomkPkgPtr pkg);
    YomkResponse stop(YomkPkgPtr pkg);
    YomkResponse post(YomkPkgPtr pkg);
    YomkResponse postWait(YomkPkgPtr pkg);
    YomkResponse destroy(YomkPkgPtr pkg);
    YomkResponse loops(YomkPkgPtr pkg);
    YomkResponse loopInfo(YomkPkgPtr pkg);
    YomkResponse listAll(YomkPkgPtr pkg);

private:
    std::map<std::string, EventLoopPtr> m_eventLoop;
    std::shared_mutex m_eventLoopMutex;
};