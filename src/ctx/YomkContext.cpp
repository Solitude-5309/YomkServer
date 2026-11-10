#include "YomkContext.h"
#include "yomk/ctx/YomkContextMsg.h"
#include "yomk/YomkDiag.h"

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
    // 排空式停止(拒新 -> 排空 -> join):返回前存量异步监控任务已全部执行;
    // stop 幂等,重复 deinit 安全
    m_main.stop();
    // 自定义组:锁内拷出指针,锁外逐组排空停止(保持"deinit 不持锁调 stop")
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

// 组路由辅助:main 组成员直取;自定义组按名惰性自动创建(不支持销毁,map 只增不减);
// 失败时 resp 携带错误响应并返回 nullptr(调用方判空即返回)
ContextGroup* YomkContext::findGroup(const std::string& groupName, yomk::payload::Response& resp)
{
    if (groupName.empty())
    {
        YOMK_DIAG_WARN("group name is empty, please check Context.m_group");
        resp = yomk::payload::Response(yomk::payload::ResStatus::Invalid, "group name is empty");
        return nullptr;
    }
    if (groupName == "main")
    {
        return &m_main;
    }

    // 非 main:读锁快查自定义组表
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        auto it = m_customGroup.find(groupName);
        if (it != m_customGroup.end())
        {
            return it->second.get();
        }
    }
    // 未命中:写锁内 double-check 后惰性创建(构造 + emplace 锁内原子完成,避免并发双建);
    // map 持有强引用且只增不减,锁外裸指针长期安全
    std::unique_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
    auto it = m_customGroup.find(groupName);
    if (it == m_customGroup.end())
    {
        it = m_customGroup.emplace(groupName, std::make_shared<ContextGroup>(groupName)).first;
    }
    return it->second.get();
}

yomk::payload::Response YomkContext::create(YomkMsgPtr pkg)
{
    auto context = yomk::msgs::unpack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not Context, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射(约定见 ContextGroup::create):0 成功 / 1 key 为空 / 2 value 为空 / 3 key 已存在
    // 三态约定:入参非法(key/value 为空)= Invalid,业务冲突(已存在)= Failed
    int rc = group->create(context->d.m_key, context->d.m_value);
    if (rc == 1 || rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid,
                                       rc == 1 ? "context key is empty"
                                               : "create context key '" + context->d.m_key + "' failed: value is empty");
    }
    if (rc == 3)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + context->d.m_key + "' already exists");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "create context success");
}

yomk::payload::Response YomkContext::destroy(YomkMsgPtr pkg)
{
    auto groupKey = yomk::msgs::unpack<yomk::msgs::ContextGroupKey>(pkg);
    if (!groupKey)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not ContextGroupKey, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(groupKey->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射(约定见 ContextGroup::destroy):0 成功 / 1 key 不存在 / 2 key 为空
    // 三态约定:入参非法(键空)= Invalid,业务未命中(不存在)= Failed
    int rc = group->destroy(groupKey->d.m_key);
    if (rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "context key is empty");
    }
    if (rc == 1)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + groupKey->d.m_key + "' does not exist");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "destroy context success");
}

yomk::payload::Response YomkContext::get(YomkMsgPtr pkg)
{
    auto context = yomk::msgs::unpack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not Context, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    YomkMsgPtr value;
    // 返回码映射(约定见 ContextGroup::get):0 命中 / 1 key 为空 / 2 key 不存在;未命中时 value 已被填为默认值
    int rc = group->get(context->d.m_key, context->d.m_value, value);
    if (rc == 1)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "context key is empty, return default value", value);
    }
    if (rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + context->d.m_key + "' does not exist, return default value", value);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "get context success", value);
}

yomk::payload::Response YomkContext::set(YomkMsgPtr pkg)
{
    auto context = yomk::msgs::unpack<yomk::msgs::Context>(pkg);
    if (!context)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not Context, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(context->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 失败文案经 detail 透传(ContextGroup::set 失败时已携带 key 值与期望/实际类型名,此处无需再分支拼装)
    // 三态约定:入参非法(键空/value 空,rc 1/5)= Invalid,业务未命中/冲突(2/3/4)= Failed
    std::string detail;
    int rc = group->set(context->d.m_key, context->d.m_value, detail);
    if (rc == 1 || rc == 5)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, detail);
    }
    if (rc != 0)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, detail);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "set context success");
}

yomk::payload::Response YomkContext::turnOnChecker(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnChecker();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "turn on checker success");
}

yomk::payload::Response YomkContext::turnOffChecker(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffChecker();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "turn off checker success");
}

yomk::payload::Response YomkContext::turnOnMonitor(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOnMonitor();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "turn on monitor success");
}

yomk::payload::Response YomkContext::turnOffMonitor(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    group->turnOffMonitor();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "turn off monitor success");
}

yomk::payload::Response YomkContext::setChecker(YomkMsgPtr pkg)
{
    auto checker = yomk::msgs::unpack<yomk::msgs::ContextChecker>(pkg);
    if (!checker)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not ContextChecker, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(checker->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射(约定见 ContextGroup::setChecker):0 成功 / 1 key 为空 / 2 checkFunc 为空 / 3 key 不存在
    // 三态约定:入参非法(键空/回调空)= Invalid
    int rc = group->setChecker(checker->d.m_key, checker->d.m_checkFunc);
    if (rc == 1 || rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid,
                                       rc == 1 ? "set checker key is empty"
                                               : "set checker key '" + checker->d.m_key + "' failed: checkFunc is empty");
    }
    if (rc == 3)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + checker->d.m_key + "' does not exist");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "set checker success");
}

yomk::payload::Response YomkContext::setMonitor(YomkMsgPtr pkg)
{
    auto monitor = yomk::msgs::unpack<yomk::msgs::ContextMonitor>(pkg);
    if (!monitor)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not ContextMonitor, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(monitor->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    // 返回码映射(约定见 ContextGroup::setMonitor):0 成功 / 1 key 为空 / 2 monitorFunc 为空 / 3 key 不存在
    // 三态约定:入参非法(键空/回调空)= Invalid
    int rc = group->setMonitor(monitor->d.m_key, monitor->d.m_contextMonitorFunc, monitor->d.m_asyncMonitor);
    if (rc == 1 || rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid,
                                       rc == 1 ? "set monitor key is empty"
                                               : "set monitor key '" + monitor->d.m_key + "' failed: monitor function is empty");
    }
    if (rc == 3)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + monitor->d.m_key + "' does not exist");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "set context monitor success");
}

yomk::payload::Response YomkContext::groups(YomkMsgPtr pkg)
{
    // 全部组名:main 恒在首位,自定义组按 map 字典序追加
    std::vector<std::string> groupNames;
    groupNames.push_back(m_main.name());
    {
        std::shared_lock<std::shared_mutex> lockGroups(m_customGroupMutex);
        for (const auto& iter : m_customGroup)
        {
            groupNames.push_back(iter.first);
        }
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(groupNames)};
}

yomk::payload::Response YomkContext::keys(YomkMsgPtr pkg)
{
    auto str = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!str)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(str->d, fail);
    if (group == nullptr)
    {
        return fail;
    }

    std::vector<std::string> keyList = group->keys();
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(keyList)};
}

yomk::payload::Response YomkContext::keyInfo(YomkMsgPtr pkg)
{
    auto groupKey = yomk::msgs::unpack<yomk::msgs::ContextGroupKey>(pkg);
    if (!groupKey)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not ContextGroupKey, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    yomk::payload::Response fail;
    ContextGroup* group = findGroup(groupKey->d.m_group, fail);
    if (group == nullptr)
    {
        return fail;
    }

    yomk::payload::ContextKeyInfo info;
    // 返回码映射(约定见 ContextGroup::keyInfo):0 命中 / 1 key 为空 / 2 key 不存在
    int rc = group->keyInfo(groupKey->d.m_key, info);
    if (rc == 1)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "context key is empty");
    }
    if (rc == 2)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "context key '" + groupKey->d.m_key + "' does not exist");
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::ContextKeyInfo::shared(info)};
}

yomk::payload::Response YomkContext::listAll(YomkMsgPtr pkg)
{
    // 跨组聚合:组 → 键元信息两层结构(对齐 srv_info().all() 的 ServerServiceInfo 范式);
    // main 组条目恒在首位,自定义组按 map 字典序追加(锁内拷指针,锁外取快照)
    std::vector<yomk::payload::ContextGroupInfo> groups;
    yomk::payload::ContextGroupInfo mainInfo;
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
        yomk::payload::ContextGroupInfo info;
        info.m_groupName = group->name();
        info.m_keys = group->listAll();
        groups.push_back(std::move(info));
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::ContextGroupInfoList::shared(std::move(groups))};
}
