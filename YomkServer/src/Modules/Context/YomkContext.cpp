#include "YomkContext.h"
#include <iostream>
#include <thread>

YomkContext::YomkContext(YomkServer *server)
    : YomkService(server), m_checkerEnabled(false), m_monitorEnabled(false)
{
    name("/YomkContext");
}

int YomkContext::init()
{
    YomkInstallFunc("/create", YomkContext::create);
    YomkInstallFunc("/destroy", YomkContext::destroy);
    YomkInstallFunc("/get", YomkContext::get);
    YomkInstallFunc("/set", YomkContext::set);
    YomkInstallFunc("/turn_on_checker", YomkContext::turnOnChecker);
    YomkInstallFunc("/turn_off_checker", YomkContext::turnOffChecker);
    YomkInstallFunc("/turn_on_monitor", YomkContext::turnOnMonitor);
    YomkInstallFunc("/turn_off_monitor", YomkContext::turnOffMonitor);
    YomkInstallFunc("/set_checker", YomkContext::setChecker);
    YomkInstallFunc("/set_monitor", YomkContext::setMonitor);
    return 0;
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
                monitor.contextMonitorFunc(context->d);
            }
            else
            {
                std::thread t(monitor.contextMonitorFunc, context->d);
                t.detach();
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
