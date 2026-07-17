#include "YomkServiceA.h"

YomkServiceA::YomkServiceA(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceA");
}

int YomkServiceA::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_a", YomkServiceA::callSkillA);
    // 日志
    YOMK_INFO_TAG("YomkServiceA::init", "install func [ /call_skill_a ] to", name());
    return 0;
}

YomkResponse YomkServiceA::callSkillA(YomkPkgPtr pkg)
{
    // 解包数据
    YomkUnPackPkgResponse(pkg, YMyServiceMsg, myServiceMsg);

    // 日志
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " exec skill a, with msg: ", myServiceMsg->msg.content);

    // 调用服务B中的方法
    YomkResponse response = YOMK_REQUEST("/YomkServiceB/call_skill_b", YomkMkPtr(YMyServiceMsg, MyServiceMsg{"hello world b"}));

    // 检查调用结果
    if (response.m_status != YomkResponse::eOk)
    {
        YOMK_ERROR_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);
        return YomkResponse(YomkResponse::eNo, name() + " exec skill a failed");
    }

    // 日志
    YOMK_INFO_TAG("YomkServiceA::callSkillA", name(), " call /YomkServiceB/call_skill_b, response: ", response.m_msg);

    // 返回结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill a success");
}
