#include "YomkContext.h"

YomkContext::YomkContext(YomkServer* server) : YomkService(server), m_main("main")
{
    name("/YomkContext");
}

int YomkContext::init()
{
    YomkInstallFunc("/create", YomkContext::create, Context);
    YomkInstallFunc("/destroy", YomkContext::destroy, ContextGroupKey);
    YomkInstallFunc("/get", YomkContext::get, Context);
    YomkInstallFunc("/set", YomkContext::set, Context);
    YomkInstallFunc("/turn_on_checker", YomkContext::turnOnChecker);
    YomkInstallFunc("/turn_off_checker", YomkContext::turnOffChecker);
    YomkInstallFunc("/turn_on_monitor", YomkContext::turnOnMonitor);
    YomkInstallFunc("/turn_off_monitor", YomkContext::turnOffMonitor);
    YomkInstallFunc("/set_checker", YomkContext::setChecker, ContextChecker);
    YomkInstallFunc("/set_monitor", YomkContext::setMonitor, ContextMonitor);
    YomkInstallFunc("/groups", YomkContext::groups);
    YomkInstallFunc("/keys", YomkContext::keys);
    YomkInstallFunc("/key", YomkContext::keyInfo, ContextGroupKey);
    YomkInstallFunc("/all", YomkContext::listAll);
    return 0;
}

void YomkContext::deinit()
{
    // 排空式停止（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
    // stop 幂等，重复 deinit 安全
    m_main.stop();
    // 自定义组：锁内拷出指针、锁外逐组排空停止（保持"deinit 不持锁调 stop"）
    std::vector<ContextGroupPtr> groups;
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        groups.reserve(m_customGroup.size());
        for (const auto& iter : m_customGroup)
        {
            groups.push_back(iter.second);
        }
    }
    for (const auto& group : groups)
    {
        group->stop();
    }
}

// 组路由辅助：main 组成员直取；自定义组按名惰性自动创建（不支持销毁，map 只增不减）；
// 失败时 resp 携带错误响应并返回 nullptr（调用方判空即返回）
ContextGroup* YomkContext::findGroup(const std::string& groupName, YomkResponse& resp)
{
    if (groupName.empty())
    {
        YOMK_ERR_POS_LOG("group name is empty, please check Context.m_group.");
        resp = YomkResponse(YomkResponse::eNo, "group name is empty");
        return nullptr;
    }
    if (groupName == "main")
    {
        return &m_main;
    }

    // 非 main：读锁快查自定义组表
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        auto it = m_customGroup.find(groupName);
        if (it != m_customGroup.end())
        {
            return it->second.get();
        }
    }
    // 未命中：写锁内 double-check 后惰性创建（构造 + emplace 锁内原子完成，避免并发双建）；
    // map 持有强引用且只增不减，锁外裸指针长期安全
    std::unique_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
    auto it = m_customGroup.find(groupName);
    if (it == m_customGroup.end())
    {
        it = m_customGroup.emplace(groupName, std::make_shared<ContextGroup>(groupName)).first;
    }
    return it->second.get();
}

YomkResponse YomkContext::create(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    YomkResponse fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::create）：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    int rc = group->create(context->d.m_key, context->d.m_value);
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
    YomkUnPackPkgResponse(pkg, ContextGroupKey, groupKey);
    YomkResponse fail;
    ContextGroup* group = findGroup(groupKey->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::destroy）：0 成功 / 1 key 不存在
    int rc = group->destroy(groupKey->d.m_key);
    if (rc != 0)
    {
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return YomkResponse(YomkResponse::eOk, "destroy context success");
}

YomkResponse YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    YomkResponse fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    YomkPkgPtr value;
    // 返回码映射（约定见 ContextGroup::get）：0 命中 / 1 key 为空 / 2 key 不存在；未命中时 value 已被填为默认值
    int rc = group->get(context->d.m_key, context->d.m_value, value);
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
    YomkResponse fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::set）：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
    int rc = group->set(context->d.m_key, context->d.m_value);
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
    YomkUnPackPkgResponse(pkg, String, str);
    YomkResponse fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnChecker();
    return YomkResponse(YomkResponse::eOk, "turn on checker success");
}

YomkResponse YomkContext::turnOffChecker(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    YomkResponse fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffChecker();
    return YomkResponse(YomkResponse::eOk, "turn off checker success");
}

YomkResponse YomkContext::turnOnMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    YomkResponse fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnMonitor();
    return YomkResponse(YomkResponse::eOk, "turn on monitor success");
}

YomkResponse YomkContext::turnOffMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    YomkResponse fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffMonitor();
    return YomkResponse(YomkResponse::eOk, "turn off monitor success");
}

YomkResponse YomkContext::setChecker(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextChecker, checker);
    YomkResponse fail;
    ContextGroup* group = findGroup(checker->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::setChecker）：0 成功 / 1 key 为空 / 2 checkFunc 为空 / 3 key 不存在
    int rc = group->setChecker(checker->d.m_key, checker->d.m_checkFunc);
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
    YomkResponse fail;
    ContextGroup* group = findGroup(monitor->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::setMonitor）：0 成功 / 1 key 为空 / 2 monitorFunc 为空 / 3 key 不存在
    int rc = group->setMonitor(monitor->d.m_key, monitor->d.m_contextMonitorFunc, monitor->d.m_asyncMonitor);
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

YomkResponse YomkContext::groups(YomkPkgPtr pkg)
{
    // 全部组名：main 恒在首位，自定义组按 map 字典序追加
    std::vector<std::string> groupNames;
    groupNames.push_back(m_main.name());
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        for (const auto& iter : m_customGroup)
        {
            groupNames.push_back(iter.first);
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, groupNames)};
}

YomkResponse YomkContext::keys(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    YomkResponse fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    std::vector<std::string> keyList = group->keys();
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, keyList)};
}

YomkResponse YomkContext::keyInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextGroupKey, groupKey);
    YomkResponse fail;
    ContextGroup* group = findGroup(groupKey->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    yomk::ContextKeyInfo info;
    // 返回码映射（约定见 ContextGroup::keyInfo）：0 命中 / 1 key 为空 / 2 key 不存在
    int rc = group->keyInfo(groupKey->d.m_key, info);
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
    // 跨组聚合：组 → 键元信息两层结构（对齐 SERVER_INFO_ALL 的 ServerServiceInfo 范式）；
    // main 组条目恒在首位，自定义组按 map 字典序追加（锁内拷指针、锁外取快照）
    std::vector<yomk::ContextGroupInfo> groups;
    yomk::ContextGroupInfo mainInfo;
    mainInfo.m_groupName = m_main.name();
    mainInfo.m_keys = m_main.listAll();
    groups.push_back(std::move(mainInfo));

    std::vector<ContextGroupPtr> customGroups;
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        customGroups.reserve(m_customGroup.size());
        for (const auto& iter : m_customGroup)
        {
            customGroups.push_back(iter.second);
        }
    }
    for (const auto& group : customGroups)
    {
        yomk::ContextGroupInfo info;
        info.m_groupName = group->name();
        info.m_keys = group->listAll();
        groups.push_back(std::move(info));
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(ContextGroupInfoList, std::move(groups))};
}
