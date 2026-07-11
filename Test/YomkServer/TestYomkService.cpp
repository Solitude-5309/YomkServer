#include "YomkAPI.h"
#include "boot/MyBoot.h"
#include "msgs/YomkMsgs.h"

int main(int argc, char *argv[])
{
    YOMK_BOOT(new MyBoot({"/YomkServiceA", "/YomkServiceB"}));

    // 同步调用服务A中的方法
    YomkResponse response = YOMK_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(MyServiceMsg, MyServiceMsg{"hello world a"}));
    if (response.m_resStatus == YomkResponse::eOk)
    {
        YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }
    else
    {
        YOMK_ERROR_TAG("main", "request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
    }

    YOMK_INFO_TAG("main", "request /YomkServiceA/call_skill_a send finished.");

    // 异步调用服务A中的方法
    YOMK_ASYNC_REQUEST("/YomkServiceA/call_skill_a", YomkMkPtr(MyServiceMsg, MyServiceMsg{"hello world a"}), [](YomkResponse response)
                       {
        if(response.m_resStatus == YomkResponse::eOk)
        {
            YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        }
        else
        {
            YOMK_ERROR_TAG("main", "async request /YomkServiceA/call_skill_a, with response.msg: ", response.m_msg);
        } });

    YOMK_INFO_TAG("main", "async request /YomkServiceA/call_skill_a send finished.");

    getchar();

    return 0;
}