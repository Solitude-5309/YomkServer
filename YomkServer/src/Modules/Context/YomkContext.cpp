#include "YomkContext.h"

YomkContext::YomkContext(YomkServer* server) : YomkService(server), m_main("main")
{
    name("/YomkContext");
}

int YomkContext::init()
{
    installFunc("/create", &YomkContext::create, "Context");
    installFunc("/destroy", &YomkContext::destroy, "ContextGroupKey");
    installFunc("/get", &YomkContext::get, "Context");
    installFunc("/set", &YomkContext::set, "Context");
    installFunc("/turn_on_checker", &YomkContext::turnOnChecker);
    installFunc("/turn_off_checker", &YomkContext::turnOffChecker);
    installFunc("/turn_on_monitor", &YomkContext::turnOnMonitor);
    installFunc("/turn_off_monitor", &YomkContext::turnOffMonitor);
    installFunc("/set_checker", &YomkContext::setChecker, "ContextChecker");
    installFunc("/set_monitor", &YomkContext::setMonitor, "ContextMonitor");
    installFunc("/groups", &YomkContext::groups);
    installFunc("/keys", &YomkContext::keys);
    installFunc("/key", &YomkContext::keyInfo, "ContextGroupKey");
    installFunc("/all", &YomkContext::listAll);
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
ContextGroup* YomkContext::findGroup(const std::string& groupName, yomk::Response& resp)
{
    if (groupName.empty())
    {
        YOMK_ERR_POS_LOG("group name is empty, please check Context.m_group.");
        resp = yomk::Response(yomk::ResStatus::Failed, "group name is empty");
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

yomk::Response YomkContext::create(YomkPkgPtr pkg)
{
    auto context = yomk::unPack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not Context"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::create）：0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    int rc = group->create(context->d.m_key, context->d.m_value);
    if (rc == 1)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is empty");
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "value is empty");
    }
    if (rc == 3)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key already exists");
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "create context success");
}

yomk::Response YomkContext::destroy(YomkPkgPtr pkg)
{
    auto groupKey = yomk::unPack<yomk::msgs::ContextGroupKey>(pkg);
    if (!groupKey)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ContextGroupKey"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(groupKey->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::destroy）：0 成功 / 1 key 不存在
    int rc = group->destroy(groupKey->d.m_key);
    if (rc != 0)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist");
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "destroy context success");
}

yomk::Response YomkContext::get(YomkPkgPtr pkg)
{
    auto context = yomk::unPack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not Context"};
    }
    yomk::Response fail;
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
        return yomk::Response(yomk::ResStatus::Failed, "key is empty, return default value.", value);
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist, return default value.", value);
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "get context success", value);
}

yomk::Response YomkContext::set(YomkPkgPtr pkg)
{
    auto context = yomk::unPack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not Context"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::set）：0 成功 / 1 key 为空 / 2 key 不存在 / 3 类型不匹配 / 4 checker 拒绝
    int rc = group->set(context->d.m_key, context->d.m_value);
    if (rc == 1)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is empty");
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist");
    }
    if (rc == 3)
    {
        return yomk::Response(yomk::ResStatus::Failed, "context type not match");
    }
    if (rc == 4)
    {
        return yomk::Response(yomk::ResStatus::Failed, "checker reject set context");
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "set context success");
}

yomk::Response YomkContext::turnOnChecker(YomkPkgPtr pkg)
{
    auto str = yomk::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnChecker();
    return yomk::Response(yomk::ResStatus::Succeeded, "turn on checker success");
}

yomk::Response YomkContext::turnOffChecker(YomkPkgPtr pkg)
{
    auto str = yomk::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffChecker();
    return yomk::Response(yomk::ResStatus::Succeeded, "turn off checker success");
}

yomk::Response YomkContext::turnOnMonitor(YomkPkgPtr pkg)
{
    auto str = yomk::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnMonitor();
    return yomk::Response(yomk::ResStatus::Succeeded, "turn on monitor success");
}

yomk::Response YomkContext::turnOffMonitor(YomkPkgPtr pkg)
{
    auto str = yomk::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffMonitor();
    return yomk::Response(yomk::ResStatus::Succeeded, "turn off monitor success");
}

yomk::Response YomkContext::setChecker(YomkPkgPtr pkg)
{
    auto checker = yomk::unPack<yomk::msgs::ContextChecker>(pkg);
    if (!checker)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ContextChecker"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(checker->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::setChecker）：0 成功 / 1 key 为空 / 2 checkFunc 为空 / 3 key 不存在
    int rc = group->setChecker(checker->d.m_key, checker->d.m_checkFunc);
    if (rc == 1)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is empty");
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "checkFunc is empty");
    }
    if (rc == 3)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist");
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "set checker success");
}

yomk::Response YomkContext::setMonitor(YomkPkgPtr pkg)
{
    auto monitor = yomk::unPack<yomk::msgs::ContextMonitor>(pkg);
    if (!monitor)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ContextMonitor"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(monitor->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射（约定见 ContextGroup::setMonitor）：0 成功 / 1 key 为空 / 2 monitorFunc 为空 / 3 key 不存在
    int rc = group->setMonitor(monitor->d.m_key, monitor->d.m_contextMonitorFunc, monitor->d.m_asyncMonitor);
    if (rc == 1)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is empty");
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "context monitor function is empty");
    }
    if (rc == 3)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist");
    }
    return yomk::Response(yomk::ResStatus::Succeeded, "set context monitor success");
}

yomk::Response YomkContext::groups(YomkPkgPtr pkg)
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
    return {yomk::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(groupNames)};
}

yomk::Response YomkContext::keys(YomkPkgPtr pkg)
{
    auto str = yomk::unPack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    yomk::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    std::vector<std::string> keyList = group->keys();
    return {yomk::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(keyList)};
}

yomk::Response YomkContext::keyInfo(YomkPkgPtr pkg)
{
    auto groupKey = yomk::unPack<yomk::msgs::ContextGroupKey>(pkg);
    if (!groupKey)
    {
        return {yomk::ResStatus::Failed, "pkg is null or pkg is not ContextGroupKey"};
    }
    yomk::Response fail;
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
        return yomk::Response(yomk::ResStatus::Failed, "key is empty");
    }
    if (rc == 2)
    {
        return yomk::Response(yomk::ResStatus::Failed, "key is not exist");
    }
    return {yomk::ResStatus::Succeeded, "ok", yomk::msgs::ContextKeyInfo::shared(info)};
}

yomk::Response YomkContext::listAll(YomkPkgPtr pkg)
{
    // 跨组聚合：组 → 键元信息两层结构（对齐 getAllServerInfo 的 ServerServiceInfo 范式）；
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
    return {yomk::ResStatus::Succeeded, "ok", yomk::msgs::ContextGroupInfoList::shared(std::move(groups))};
}
