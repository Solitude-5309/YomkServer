#include <iostream>
#include <thread>
#include "YomkAPI.h"

YomkResponse eventHandle(YomkPkgPtr pkg)
{
    YOMK_DEBUG_TAG("eventHandle", "eventHandle called by thread: ", std::this_thread::get_id());

    YomkUnPackPkgResponse(pkg, string, str);

    if(!str)
    {
        return YomkResponse(YomkResponse::eInvalid, "string is null");
    }

    YOMK_DEBUG_TAG("eventHandle", "eventHandle called with data: ", str->d);

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
        YOMK_DEBUG_TAG("main", "start event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "start event_loop_1 failed: ", response.m_msg);
    }

    response = YOMK_EVENTLOOP_POST("event_loop_1", YomkMkPtr(string, "requestEventHandle_data"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "post to event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "post to event_loop_1 failed: ", response.m_msg);
    }

    response = YOMK_EVENTLOOP_POST_WAIT("event_loop_1", YomkMkPtr(string, "requestEventHandle_data_wait"));
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "post_wait to event_loop_1 success");
        YomkUnPackPkg(response.m_data, Event, event);
        if(event)
        {
            YOMK_DEBUG_TAG(
                "main", 
                "post_wait response eventId: ", 
                event->d.m_eventId,
                " eventLoopName: " , 
                event->d.m_eventLoopName,
                " response: " , event->d.m_response.m_msg);
        }
    }
    else
    {
        YOMK_ERROR_TAG("main", "post_wait to event_loop_1 failed: ", response.m_msg);
    }

    YOMK_DEBUG_TAG("main", "enter any key to stop event_loop_1");
    getchar();
    
    response = YOMK_EVENTLOOP_STOP("event_loop_1");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "stop event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "stop event_loop_1 failed: ", response.m_msg);
    }

    YOMK_DEBUG_TAG("main", "enter any key to destroy event_loop_1");
    getchar();

    response = YOMK_EVENTLOOP_DESTROY("event_loop_1");
    if(response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_DEBUG_TAG("main", "destroy event_loop_1 success");
    }
    else
    {
        YOMK_ERROR_TAG("main", "destroy event_loop_1 failed: ", response.m_msg);
    }
    
    YOMK_DEBUG_TAG("main", "test YomkEventLoop completed, any key to continue...");

    getchar();

    return 0;
}
