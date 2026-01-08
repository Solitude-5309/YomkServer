#include <iostream>
#include <thread>
#include "YomkServer.h"
#include "YomkDefine.h"

YomkResponse requestEventHandle(YomkPkgPtr pkg)
{
    std::cout << "requestEventHandle called by thread: " << std::this_thread::get_id() << std::endl;

    YomkUnPackPkgresponse(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkResponse(YomkResponse::eInvalid, "YString is null");
    }

    std::cout << "requestEventHandle called with data: " << yString->d << std::endl;

    return {YomkResponse::eOk, "requestEventHandle success. "};
}

void eventHandleFinished(std::shared_ptr<YomkEvent> eventPtr)
{
    std::cout << "eventHandleFinished called by thread: " << std::this_thread::get_id() << std::endl;

    if(eventPtr->name() == "YResquestEvent")
    {
        YomkUnPackPkgVoid(eventPtr, "YResquestEvent", YResquestEvent, yResquestEvent);
        if(yResquestEvent)
        {
            std::cout << "eventHandleFinished called with eventId: " << eventPtr->m_eventId << " eventLoopName: " << eventPtr->m_eventLoopName << " response: " << yResquestEvent->m_response.m_msg << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    YomkServer server;
    server.startService({ "/YomkSettings", "/YomkFunctionPool", "/YomkEventLoop" });
    
    YomkResponse response = server.request("/YomkEventLoop/start", YomkMkYStringPtr("event_loop_1"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "start event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "start event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = server.request("/YomkEventLoop/post", YomkMkYResquestEventPtr("event_loop_1", YomkMkYStringPtr("requestEventHandle_data"), requestEventHandle, eventHandleFinished));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post to event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "post to event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = server.request("/YomkEventLoop/post_wait", YomkMkYResquestEventPtr("event_loop_1", YomkMkYStringPtr("requestEventHandle_data_wait"), requestEventHandle, eventHandleFinished));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post_wait to event_loop_1 success" << std::endl;
        YomkUnPackPkg(response.m_data, "YResquestEvent", YResquestEvent, yResquestEvent);
        if(yResquestEvent)
        {
            std::cout << "post_wait response eventId: " << yResquestEvent->m_eventId << " eventLoopName: " << yResquestEvent->m_eventLoopName << " response: " << yResquestEvent->m_response.m_msg << std::endl;
        }
    }
    else
    {
        std::cout << "post_wait to event_loop_1 failed: " << response.m_msg << std::endl;
    }

    std::cout << "enter any key to stop event_loop_1" << std::endl;
    getchar();
    
    response = server.request("/YomkEventLoop/stop", YomkMkYStringPtr("event_loop_1"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "stop event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "stop event_loop_1 failed: " << response.m_msg << std::endl;
    }

    std::cout << "enter any key to destroy event_loop_1" << std::endl;
    getchar();

    response = server.request("/YomkEventLoop/destroy", YomkMkYStringPtr("event_loop_1"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "destroy event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "destroy event_loop_1 failed: " << response.m_msg << std::endl;
    }
    
    std::cout << "test YomkEventLoop completed, any key to continue..." << std::endl;

    getchar();

    return 0;
}
