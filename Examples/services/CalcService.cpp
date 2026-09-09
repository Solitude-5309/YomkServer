#include "services/CalcService.h"

CalcService::CalcService(YomkServer *server)
    : YomkService(server)
{
    name("/CalcService"); // 服务名：URL 前缀，全局唯一
}

int CalcService::init()
{
    // 2 参注册：无消息元数据；3 参注册：声明 /add 期望 AddReq，供自省识别
    YomkInstallFunc("/echo", CalcService::echo);
    YomkInstallFunc("/add", CalcService::add, AddReq);
    YOMK_INFO_TAG("svc.calc", "init: install /echo (no meta), /add [AddReq]");
    return 0;
}

YomkResponse CalcService::echo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    if (!str)
    {
        return {YomkResponse::eNo, "pkg is not String"};
    }
    YOMK_INFO_TAG("svc.calc", name(), " echo got: ", str->d);
    return {YomkResponse::eOk, "echo success", YomkMkPtr(String, "echo: " + str->d)};
}

YomkResponse CalcService::add(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, AddReq, reqPkg);
    if (!reqPkg)
    {
        return {YomkResponse::eNo, "pkg is not AddReq"};
    }
    YOMK_INFO_TAG("svc.calc", name(), " add got: ", reqPkg->req.a, " + ", reqPkg->req.b);

    // 跨服务调用：URL 直达 /AuditService/audit；审计缺失时失败沿调用链传播
    YomkResponse auditResp = request("/AuditService/audit", YomkMkPtr(String, "add was called"));
    if (auditResp.m_status != YomkResponse::eOk)
    {
        YOMK_INFO_TAG("svc.calc", name(), " audit failed: ", auditResp.m_msg);
        return {YomkResponse::eNo, "add failed: " + auditResp.m_msg};
    }
    YOMK_INFO_TAG("svc.calc", name(), " audit response: ", auditResp.m_msg);

    return {YomkResponse::eOk, "add success",
            YomkMkPtr(AddResp, AddResp{static_cast<std::int64_t>(reqPkg->req.a) + reqPkg->req.b})};
}
