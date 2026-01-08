#include "YomkServer.h"
#include "YomkDefine.h"
#include "EventLoop.h"
#include <map>

class YomkEventLoop : public YomkService
{
public:
    YomkEventLoop(YomkServer* server);
    ~YomkEventLoop() {}
public:
    virtual int init() override;
private:
    YomkResponse start(YomkPkgPtr pkg);
    YomkResponse stop(YomkPkgPtr pkg);
    YomkResponse post(YomkPkgPtr pkg);
    YomkResponse postWait(YomkPkgPtr pkg);
    YomkResponse destroy(YomkPkgPtr pkg);
private:
    std::map<std::string, EventLoopPtr> m_eventLoop;
};