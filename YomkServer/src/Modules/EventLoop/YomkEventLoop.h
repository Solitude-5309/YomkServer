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
    YomkRespond start(YomkPkgPtr pkg);
    YomkRespond stop(YomkPkgPtr pkg);
    YomkRespond post(YomkPkgPtr pkg);
    YomkRespond postWait(YomkPkgPtr pkg);
    YomkRespond destroy(YomkPkgPtr pkg);
private:
    std::map<std::string, EventLoopPtr> m_eventLoop;
};