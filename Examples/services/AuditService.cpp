#include "services/AuditService.h"

int AuditService::s_instance = 0;

AuditService::AuditService(YomkServer* server) : YomkService(server), m_id(++s_instance)
{
    name("/AuditService");  // 服务名：URL 前缀，全局唯一
    YOMK_INFO_TAG("svc.audit", "audit instance #", m_id, " created");
}

AuditService::~AuditService()
{
    YOMK_INFO_TAG("svc.audit", "audit instance #", m_id, " destroyed");
    --s_instance;
}

int AuditService::init()
{
    // 注册功能函数：注册后即可通过 URL /AuditService/audit 调用
    YomkInstallFunc("/audit", AuditService::audit);

    // 弱绑定注册到 FunctionPool：服务删除后 audit_hook 自动失效，不悬垂 this
    YOMK_FUNCTIONPOOL_REGISTER("audit_hook", YomkBindWeakSelf(AuditService::audit));

    YOMK_INFO_TAG("svc.audit", "init: install /audit, register weak-bound audit_hook");
    return 0;
}

void AuditService::deinit()
{
    YOMK_INFO_TAG("svc.audit", "deinit called");
}

YomkResponse AuditService::audit(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);  // String 为框架内置消息类型
    YOMK_INFO_TAG("svc.audit", name(), " audit recorded: ", (str ? str->d : "(null)"));
    return {YomkResponse::eOk, "audit recorded"};
}
