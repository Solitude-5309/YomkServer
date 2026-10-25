#include "YomkContext.h"

YomkContext::YomkContext(YomkServer* server) : YomkService(server), m_main("main")
{
    name("/YomkContext");
}

int YomkContext::init()
{
    YomkInstallFunc("/create", YomkContext::create, Context);
    YomkInstallFunc("/destroy", YomkContext::destroy, String);
    YomkInstallFunc("/get", YomkContext::get, Context);
    YomkInstallFunc("/set", YomkContext::set, Context);
    YomkInstallFunc("/turn_on_checker", YomkContext::turnOnChecker);
    YomkInstallFunc("/turn_off_checker", YomkContext::turnOffChecker);
    YomkInstallFunc("/turn_on_monitor", YomkContext::turnOnMonitor);
    YomkInstallFunc("/turn_off_monitor", YomkContext::turnOffMonitor);
    YomkInstallFunc("/set_checker", YomkContext::setChecker, ContextChecker);
    YomkInstallFunc("/set_monitor", YomkContext::setMonitor, ContextMonitor);
    YomkInstallFunc("/keys", YomkContext::keys);
    YomkInstallFunc("/key", YomkContext::keyInfo, String);
    YomkInstallFunc("/all", YomkContext::listAll);
    return 0;
}

void YomkContext::deinit()
{
    // 排空式停止（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
    // stop 幂等，重复 deinit 安全
    m_main.stop();
}

YomkResponse YomkContext::create(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    // 返回码映射（约定见 ContextGroup::create）：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    int rc = m_main.create(context->d.m_key, context->d.m_value);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "value is empty");
    }
    if (rc == 3)
    {
        return YomkResponse(YomkResponse::eNo, "key already exists");
    }
    return YomkResponse(YomkResponse::eOk, "create context success");
}

YomkResponse YomkContext::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    // 返回码映射（约定见 ContextGroup::destroy）：0 成功 / 1 key 不存在
    int rc = m_main.destroy(str->d);
    if (rc != 0)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return YomkResponse(YomkResponse::eOk, "destroy context success");
}

YomkResponse YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    YomkPkgPtr value;
    // 返回码映射（约定见 ContextGroup::get）：0 命中 / 1 key 为空 / 2 key 不存在；未命中时 value 已被填为默认值
    int rc = m_main.get(context->d.m_key, context->d.m_value, value);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty, return default value.", value);
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist, return default value.", value);
    }
    return YomkResponse(YomkResponse::eOk, "get context success", value);
}

YomkResponse YomkContext::set(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    // 返回码映射（约定见 ContextGroup::set）：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
    int rc = m_main.set(context->d.m_key, context->d.m_value);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    if (rc == 3)
    {
        return YomkResponse(YomkResponse::eNo, "context type not match");
    }
    if (rc == 4)
    {
        return YomkResponse(YomkResponse::eNo, "checker reject set context");
    }
    return YomkResponse(YomkResponse::eOk, "set context success");
}

YomkResponse YomkContext::turnOnChecker(YomkPkgPtr pkg)
{
    m_main.turnOnChecker();
    return YomkResponse(YomkResponse::eOk, "turn on checker success");
}

YomkResponse YomkContext::turnOffChecker(YomkPkgPtr pkg)
{
    m_main.turnOffChecker();
    return YomkResponse(YomkResponse::eOk, "turn off checker success");
}

YomkResponse YomkContext::turnOnMonitor(YomkPkgPtr pkg)
{
    m_main.turnOnMonitor();
    return YomkResponse(YomkResponse::eOk, "turn on monitor success");
}

YomkResponse YomkContext::turnOffMonitor(YomkPkgPtr pkg)
{
    m_main.turnOffMonitor();
    return YomkResponse(YomkResponse::eOk, "turn off monitor success");
}

YomkResponse YomkContext::setChecker(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextChecker, checker);
    // 返回码映射（约定见 ContextGroup::setChecker）：0 成功 / 1 key 为空 / 2 checkFunc 为空 / 3 key 不存在
    int rc = m_main.setChecker(checker->d.m_key, checker->d.m_checkFunc);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "checkFunc is empty");
    }
    if (rc == 3)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return YomkResponse(YomkResponse::eOk, "set checker success");
}

YomkResponse YomkContext::setMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextMonitor, monitor);
    // 返回码映射（约定见 ContextGroup::setMonitor）：0 成功 / 1 key 为空 / 2 monitorFunc 为空 / 3 key 不存在
    int rc = m_main.setMonitor(monitor->d.m_key, monitor->d.m_contextMonitorFunc, monitor->d.m_asyncMonitor);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "context monitor function is empty");
    }
    if (rc == 3)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return YomkResponse(YomkResponse::eOk, "set context monitor success");
}

YomkResponse YomkContext::keys(YomkPkgPtr pkg)
{
    std::vector<std::string> keyList = m_main.keys();
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, keyList)};
}

YomkResponse YomkContext::keyInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    yomk::ContextKeyInfo info;
    // 返回码映射（约定见 ContextGroup::keyInfo）：0 命中 / 1 key 为空 / 2 key 不存在
    int rc = m_main.keyInfo(str->d, info);
    if (rc == 1)
    {
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (rc == 2)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(ContextKeyInfo, info)};
}

YomkResponse YomkContext::listAll(YomkPkgPtr pkg)
{
    std::vector<yomk::ContextKeyInfo> infos = m_main.listAll();
    return {YomkResponse::eOk, "ok", YomkMkPtr(ContextKeyInfoList, infos)};
}
