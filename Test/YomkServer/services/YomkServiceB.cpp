#include "YomkServiceB.h"

YomkServiceB::YomkServiceB(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServiceB");
}

int YomkServiceB::init()
{
    // 安装功能函数，功能函数名称在服务中必须唯一
    YomkInstallFunc("/call_skill_b", YomkServiceB::callSkillB);
    // 日志
    YOMK_INFO_TAG("YomkServiceB::init", "install func [ /call_skill_b ] to", name());
    return 0;
}

YomkResponse YomkServiceB::callSkillB(YomkPkgPtr pkg)
{
    // 解包数据
    YomkUnPackPkgResponse(pkg, MyServiceMsg, myServiceMsg);
    if (!myServiceMsg)
    {
        YOMK_ERROR_TAG("YomkServiceB::callSkillB", name(), " myServiceMsg is empty");
        return YomkResponse(YomkResponse::eInvalid, name() + " MyServiceMsg is empty");
    }

    // 日志
    YOMK_INFO_TAG("YomkServiceB::callSkillB", name(), " exec skill b, with msg: ", myServiceMsg->serviceMsg.content);

    // 返回结果
    return YomkResponse(YomkResponse::eOk, name() + " exec skill b success");
}
