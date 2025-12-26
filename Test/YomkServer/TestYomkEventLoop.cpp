#include <iostream>
#include <thread>
#include "YomkServer.h"
#include "YomkDefine.h"

YomkRespond requestEventHandle(YomkPkgPtr pkg)
{
    std::cout << "requestEventHandle called by thread: " << std::this_thread::get_id() << std::endl;

    YomkUnPackPkgRespond(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkRespond(YomkRespond::eInvalid, "YString is null");
    }

    std::cout << "requestEventHandle called with data: " << yString->d << std::endl;

    return {YomkRespond::eOk, "requestEventHandle success. "};
}

void eventHandleFinished(std::shared_ptr<YomkEvent> eventPtr)
{
    std::cout << "eventHandleFinished called by thread: " << std::this_thread::get_id() << std::endl;

    if(eventPtr->name() == "YResquestEvent")
    {
        YomkUnPackPkgVoid(eventPtr, "YResquestEvent", YResquestEvent, yResquestEvent);
        if(yResquestEvent)
        {
            std::cout << "eventHandleFinished called with eventId: " << eventPtr->m_eventId << " eventLoopName: " << eventPtr->m_eventLoopName << " respond: " << yResquestEvent->m_respond.m_msg << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    YomkServer server;
    server.startService({ "/YomkSettings", "/YomkFunctionPool", "/YomkEventLoop" });
    
    YomkRespond respond = server.request("/YomkEventLoop/start", YomkMkYStringPtr("event_loop_1"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "start event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "start event_loop_1 failed: " << respond.m_msg << std::endl;
    }

    respond = server.request("/YomkEventLoop/post", YomkMkYResquestEventPtr("event_loop_1", YomkMkYStringPtr("requestEventHandle_data"), requestEventHandle, eventHandleFinished));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "post to event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "post to event_loop_1 failed: " << respond.m_msg << std::endl;
    }

    respond = server.request("/YomkEventLoop/post_wait", YomkMkYResquestEventPtr("event_loop_1", YomkMkYStringPtr("requestEventHandle_data_wait"), requestEventHandle, eventHandleFinished));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "post_wait to event_loop_1 success" << std::endl;
        YomkUnPackPkg(respond.m_data, "YResquestEvent", YResquestEvent, yResquestEvent);
        if(yResquestEvent)
        {
            std::cout << "post_wait respond eventId: " << yResquestEvent->m_eventId << " eventLoopName: " << yResquestEvent->m_eventLoopName << " respond: " << yResquestEvent->m_respond.m_msg << std::endl;
        }
    }
    else
    {
        std::cout << "post_wait to event_loop_1 failed: " << respond.m_msg << std::endl;
    }

    std::cout << "enter any key to stop event_loop_1" << std::endl;
    getchar();
    
    respond = server.request("/YomkEventLoop/stop", YomkMkYStringPtr("event_loop_1"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "stop event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "stop event_loop_1 failed: " << respond.m_msg << std::endl;
    }

    std::cout << "enter any key to destroy event_loop_1" << std::endl;
    getchar();

    respond = server.request("/YomkEventLoop/destroy", YomkMkYStringPtr("event_loop_1"));
    if(respond.m_resStatus == YomkRespond::eOk)
    {
        std::cout << "destroy event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "destroy event_loop_1 failed: " << respond.m_msg << std::endl;
    }
    
    std::cout << "test YomkEventLoop completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
