#include <iostream>
#include <thread>
#include "YomkAPI.h"

YomkResponse eventHandle(YomkPkgPtr pkg)
{
    std::cout << "eventHandle called by thread: " << std::this_thread::get_id() << std::endl;

    YomkUnPackPkgresponse(pkg, "YString", YString, yString);

    if(!yString)
    {
        return YomkResponse(YomkResponse::eInvalid, "YString is null");
    }

    std::cout << "eventHandle called with data: " << yString->d << std::endl;

    return {YomkResponse::eOk, "eventHandle success. "};
}

void eventHandleFinished(std::shared_ptr<YomkEvent> eventPtr)
{
    std::cout << "eventHandleFinished called by thread: " << std::this_thread::get_id() << std::endl;

    if(eventPtr->name() == "YomkEvent")
    {
        YomkUnPackPkgVoid(eventPtr, "YomkEvent", YomkEvent, yomkEvent);
        if(yomkEvent)
        {
            std::cout << "eventHandleFinished called with eventId: " << eventPtr->m_eventId << " eventLoopName: " << eventPtr->m_eventLoopName << " response: " << yomkEvent->m_response.m_msg << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    std::shared_ptr<YomkServer> server = std::make_shared<YomkServer>();
    server->startService({ 
        "/YomkSettings", 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    YOMK_INIT(server);
    
    YomkResponse response = YOMK_EVENTLOOP_START(
        "event_loop_1",
        eventHandle,
        eventHandleFinished);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "start event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "start event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = YOMK_EVENTLOOP_POST("event_loop_1", YomkMkYStringPtr("requestEventHandle_data"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post to event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "post to event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = YOMK_EVENTLOOP_POST_WAIT("event_loop_1", YomkMkYStringPtr("requestEventHandle_data_wait"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post_wait to event_loop_1 success" << std::endl;
        YomkUnPackPkg(response.m_data, "YomkEvent", YomkEvent, yomkEvent);
        if(yomkEvent)
        {
            std::cout << "post_wait response eventId: " << yomkEvent->m_eventId << " eventLoopName: " << yomkEvent->m_eventLoopName << " response: " << yomkEvent->m_response.m_msg << std::endl;
        }
    }
    else
    {
        std::cout << "post_wait to event_loop_1 failed: " << response.m_msg << std::endl;
    }

    std::cout << "enter any key to stop event_loop_1" << std::endl;
    getchar();
    
    response = YOMK_EVENTLOOP_STOP("event_loop_1");
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

    response = YOMK_EVENTLOOP_DESTROY("event_loop_1");
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
