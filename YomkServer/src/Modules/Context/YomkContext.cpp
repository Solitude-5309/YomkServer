#include "YomkContext.h"
#include <exception>
#include <iostream>

// 定点引入所需类型，避免在头文件中 using namespace（第十一轮）
using yomk::ContextChecker;

YomkContext::YomkContext(YomkServer *server)
    : YomkService(server), m_checkerEnabled(false), m_monitorEnabled(false)
{
    name("/YomkContext");
}

int YomkContext::init()
{
    // 重建监控池（第十九轮）：池 stop 后不可复用，重建支持服务删除后重新注册；固定单线程保事件顺序
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
    m_monitorPool = std::make_unique<YomkSimpleThreadPool>(1);
    return 0;
}

void YomkContext::deinit()
{
    // 排空式停止（拒新 -> 排空 -> join）：返回前存量异步监控任务已全部执行；
    // stop 幂等，重复 deinit 安全（第十九轮）
    if (m_monitorPool)
    {
        m_monitorPool->stop();
    }
}

// 组装单个 key 的元信息行：key [类型名] checker:on|off monitors:N(async:M)
// 类型名取 value 的消息类型，value 为空时防御显示空类型
static std::string contextInfoLine(const YomkContext::Context &ctx)
{
    std::string typeName = ctx.value ? ctx.value->name() : "";
    size_t asyncCount = 0;
    for (auto &monitor : ctx.monitors)
    {
        if (monitor.asyncMonitor)
        {
            ++asyncCount;
        }
    }
    return ctx.key + " [" + typeName + "] " + (ctx.checker ? "checker:on" : "checker:off") +
           " monitors:" + std::to_string(ctx.monitors.size()) + "(async:" + std::to_string(asyncCount) + ")";
}

YomkResponse YomkContext::create(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);

    if (context->d.m_key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (context->d.m_value == nullptr)
    {
        YOMK_ERR_POS_LOG("value is empty, please check Context.m_value.");
        return YomkResponse(YomkResponse::eNo, "value is empty");
    }

    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if (m_contexts.find(context->d.m_key) != m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + context->d.m_key + " already exists, please check Context.m_key.");
            return YomkResponse(YomkResponse::eNo, "key already exists");
        }

        Context ctx;
        ctx.key = context->d.m_key;
        ctx.value = context->d.m_value;
        ctx.checker = nullptr;
        ctx.monitors = {};

        m_contexts.emplace(context->d.m_key, ctx);
    }

    return YomkResponse(YomkResponse::eOk, "create context success");
}

YomkResponse YomkContext::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(str->d);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + str->d + " is not exist, please check key.");
            return YomkResponse(YomkResponse::eNo, "key is not exist");
        }
        m_contexts.erase(itContext);
    }

    return YomkResponse(YomkResponse::eOk, "destroy context success");
}

YomkResponse YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);

    if (context->d.m_key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is empty, return default value.", context->d.m_value);
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);

    auto itContext = m_contexts.find(context->d.m_key);
    if (itContext == m_contexts.end())
    {
        YOMK_ERR_POS_LOG("YomkContext key: " + context->d.m_key + " is not exist, please check Context.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is not exist, return default value.", context->d.m_value);
    }

    return {YomkResponse::eOk, "get context success", itContext->second.value};
}

YomkResponse YomkContext::set(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    if (context->d.m_key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check Context.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }

    std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(context->d.m_key);
    if (itContext == m_contexts.end())
    {
        YOMK_ERR_POS_LOG("YomkContext key: " + context->d.m_key + " is not exist, please check Context.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }

    if (m_checkerEnabled.load())
    {
        ContextChecker::ECheckStatus checkStatus = itContext->second.checker(context->d);
        if (checkStatus == ContextChecker::eReject)
        {
            return YomkResponse(YomkResponse::eNo, "checker reject set context");
        }
    }

    if (itContext->second.value->name() != context->d.m_value->name())
    {
        YOMK_ERR_POS_LOG("context: " + context->d.m_key + " type not match, please check Context.m_value.");
        return YomkResponse(YomkResponse::eNo, "context type not match");
    }
    itContext->second.value = context->d.m_value;
    std::vector<ContextMonitor> monitors = itContext->second.monitors;
    lockContexts.unlock();

    if (m_monitorEnabled.load())
    {
        for (auto &monitor : monitors)
        {
            if (!monitor.asyncMonitor)
            {
                // 同步 monitor 异常防护（第十七轮）：用户回调异常记日志吞掉，不击穿 set 调用链，
                // 与异步路径线程池兜底对齐；单个回调异常不影响后续 monitor 执行
                try
                {
                    monitor.contextMonitorFunc(context->d);
                }
                catch (const std::exception &e)
                {
                    YOMK_ERR_POS_LOG("sync monitor threw exception: " + std::string(e.what()) + ", ignored.");
                }
                catch (...)
                {
                    YOMK_ERR_POS_LOG("sync monitor threw unknown exception, ignored.");
                }
            }
            else
            {
                // 异步 monitor 投 Context 模块自持的监控池（固定单线程保事件顺序，排空式停止，第十九轮）；
                // 值捕获回调与数据副本，任务执行期自包含，池已停止时投递被拒绝并记日志丢弃
                auto monitorFunc = monitor.contextMonitorFunc;
                yomk::Context data = context->d;
                if (!(m_monitorPool && m_monitorPool->post([monitorFunc, data]()
                                                           { monitorFunc(data); })))
                {
                    YOMK_ERR_POS_LOG("context monitor pool is stopped, async monitor ignored.");
                }
            }
        }
    }

    return YomkResponse(YomkResponse::eOk, "set context success");
}

YomkResponse YomkContext::turnOnChecker(YomkPkgPtr pkg)
{
    m_checkerEnabled.store(true);
    return YomkResponse(YomkResponse::eOk, "turn on checker success");
}

YomkResponse YomkContext::turnOffChecker(YomkPkgPtr pkg)
{
    m_checkerEnabled.store(false);
    return YomkResponse(YomkResponse::eOk, "turn off checker success");
}

YomkResponse YomkContext::turnOnMonitor(YomkPkgPtr pkg)
{
    m_monitorEnabled.store(true);
    return YomkResponse(YomkResponse::eOk, "turn on monitor success");
}

YomkResponse YomkContext::turnOffMonitor(YomkPkgPtr pkg)
{
    m_monitorEnabled.store(false);
    return YomkResponse(YomkResponse::eOk, "turn off monitor success");
}

YomkResponse YomkContext::setChecker(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextChecker, checker);
    if (checker->d.m_key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check ContextChecker.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (checker->d.m_checkFunc == nullptr)
    {
        YOMK_ERR_POS_LOG("checkFunc is empty, please check ContextChecker.m_checkFunc.");
        return YomkResponse(YomkResponse::eNo, "checkFunc is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(checker->d.m_key);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + checker->d.m_key + " is not exist, please check ContextChecker.m_key.");
            return YomkResponse(YomkResponse::eNo, "key is not exist");
        }

        itContext->second.checker = checker->d.m_checkFunc;
    }

    return YomkResponse(YomkResponse::eOk, "set checker success");
}

YomkResponse YomkContext::setMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextMonitor, monitor);
    if (monitor->d.m_key.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check ContextMonitor.m_key.");
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }
    if (monitor->d.m_contextMonitorFunc == nullptr)
    {
        YOMK_ERR_POS_LOG("context monitor function is empty, please check ContextMonitor.m_contextMonitorFunc.");
        return YomkResponse(YomkResponse::eNo, "context monitor function is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(monitor->d.m_key);
        if (itContext == m_contexts.end())
        {
            YOMK_ERR_POS_LOG("YomkContext key: " + monitor->d.m_key + " is not exist, please check ContextMonitor.m_key.");
            return YomkResponse(YomkResponse::eNo, "key is not exist");
        }
        ContextMonitor contextMonitor;
        contextMonitor.contextMonitorFunc = monitor->d.m_contextMonitorFunc;
        contextMonitor.asyncMonitor = monitor->d.m_asyncMonitor;
        itContext->second.monitors.push_back(contextMonitor);
    }
    return YomkResponse(YomkResponse::eOk, "set context monitor success");
}

YomkResponse YomkContext::keys(YomkPkgPtr pkg)
{
    std::vector<std::string> keyList;
    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        for (auto &iter : m_contexts)
        {
            keyList.push_back(iter.first);
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, keyList)};
}

YomkResponse YomkContext::keyInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    if (str->d.empty())
    {
        YOMK_ERR_POS_LOG("key is empty, please check String.d.");
        return YomkResponse(YomkResponse::eNo, "key is empty");
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(str->d);
    if (itContext == m_contexts.end())
    {
        YOMK_ERR_POS_LOG("YomkContext key: " + str->d + " is not exist, please check key.");
        return YomkResponse(YomkResponse::eNo, "key is not exist");
    }
    return {YomkResponse::eOk, contextInfoLine(itContext->second)};
}

YomkResponse YomkContext::listAll(YomkPkgPtr pkg)
{
    std::vector<std::string> lines;
    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        for (auto &iter : m_contexts)
        {
            lines.push_back(contextInfoLine(iter.second));
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}
