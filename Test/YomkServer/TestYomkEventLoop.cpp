#include <iostream>
#include <thread>
#include "YomkAPI.h"

YomkResponse eventHandle(YomkPkgPtr pkg)
{
    std::cout << "eventHandle called by thread: " << std::this_thread::get_id() << std::endl;

    YomkUnPackPkgResponse(pkg, string, str);

    if(!str)
    {
        return YomkResponse(YomkResponse::eInvalid, "string is null");
    }

    std::cout << "eventHandle called with data: " << str->d << std::endl;

    static int i = 0;

    if(i++ < 3)
    {
        YOMK_EVENTLOOP_POST("event_loop_1", YomkMkPtr(string, "requestEventHandle_data_" + std::to_string(i)));
    }

    return {YomkResponse::eOk, "eventHandle success. "};
}

int main(int argc, char *argv[])
{
    YOMK_INIT(std::make_shared<YomkServer>(), { 
        "/YomkFunctionPool", 
        "/YomkContext",
        "/YomkEventLoop",
        "/YomkLogger"
    });
    
    YomkResponse response = YOMK_EVENTLOOP_START(
        "event_loop_1",
        eventHandle);
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "start event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "start event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = YOMK_EVENTLOOP_POST("event_loop_1", YomkMkPtr(string, "requestEventHandle_data"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post to event_loop_1 success" << std::endl;
    }
    else
    {
        std::cout << "post to event_loop_1 failed: " << response.m_msg << std::endl;
    }

    response = YOMK_EVENTLOOP_POST_WAIT("event_loop_1", YomkMkPtr(string, "requestEventHandle_data_wait"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        std::cout << "post_wait to event_loop_1 success" << std::endl;
        YomkUnPackPkg(response.m_data, Event, event);
        if(event)
        {
            std::cout << "post_wait response eventId: " << event->d.m_eventId 
                        << " eventLoopName: " << event->d.m_eventLoopName 
                        << " response: " << event->d.m_response.m_msg << std::endl;
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
