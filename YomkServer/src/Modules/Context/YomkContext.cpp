#include "YomkContext.h"

YomkContext::YomkContext(YomkServer *server)
    : YomkService(server)
    , m_checkerEnabled(false)
    , m_monitorEnabled(false)
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
    YomkUnPackPkgresponse(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(yContext->m_value == nullptr)
    {
        return YomkResponse(YomkResponse::eErr, "value is empty");
    }

    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if (m_contexts.find(yContext->m_key) != m_contexts.end())
        {
            return YomkResponse(YomkResponse::eErr, "key already exists");
        }

        m_contexts.emplace(yContext->m_key, yContext->m_value);
    }

    return YomkResponse(YomkResponse::eOk, "create context success");
}

YomkResponse YomkContext::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);

    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(yStr->d);
        if(itContext == m_contexts.end())
        {
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
        m_contexts.erase(itContext); 
    }

    return YomkResponse(YomkResponse::eOk, "destroy context success");
}

YomkResponse YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkResponse(YomkResponse::eErr, "key is empty", yContext->m_value);
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);

    auto itContext = m_contexts.find(yContext->m_key);
    if(itContext == m_contexts.end())
    {
        return YomkResponse(YomkResponse::eErr, "key is not exist", yContext->m_value);
    }

    return {YomkResponse::eOk, "get context success", itContext->second};
}

YomkResponse YomkContext::set(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }

    std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(yContext->m_key);
    if(itContext == m_contexts.end())
    {
        return YomkResponse(YomkResponse::eErr, "key is not exist");
    }

    if(m_checkerEnabled.load())
    {
        std::shared_lock<std::shared_mutex> checkerLock(m_checkersMutex);
        auto itChecker = m_checkers.find(yContext->m_key);
        if(itChecker != m_checkers.end())
        {
            YContextSetChecker::ECheckStatus checkStatus = itChecker->second(yContext->m_value);
            if(checkStatus == YContextSetChecker::eReject)
            {
                return YomkResponse(YomkResponse::eErr, "checker reject set context");
            }
        }
    }

    if(itContext->second->name() != yContext->m_value->name())
    {
        return YomkResponse(YomkResponse::eErr, "context type not match");
    }
    itContext->second = yContext->m_value;
    lockContexts.unlock();

    if(m_monitorEnabled.load())
    {
        std::shared_lock<std::shared_mutex> monitorLock(m_contextMonitorsMutex);
        auto itMonitor = m_contextMonitors.find(yContext->m_key);
        if(itMonitor != m_contextMonitors.end())
        {
            for(auto &monitorFunc : itMonitor->second)
            {
                monitorFunc(yContext);
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
    YomkUnPackPkgresponse(pkg, "YContextSetChecker", YContextSetChecker, yChecker);

    if(yChecker->m_key.empty())
    {
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(yChecker->m_checkFunc == nullptr)
    {
        return YomkResponse(YomkResponse::eErr, "checkFunc is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if(m_contexts.find(yChecker->m_key) == m_contexts.end())
        {
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
    }

    {
        std::unique_lock<std::shared_mutex> checkerLock(m_checkersMutex);
        m_checkers[yChecker->m_key] = yChecker->m_checkFunc;
    }

    return YomkResponse(YomkResponse::eOk, "set checker success");
}

YomkResponse YomkContext::setMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YContextMonitor", YContextMonitor, yMonitor);

    if(yMonitor->m_key.empty())
    {
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(yMonitor->m_contextMonitorFunc == nullptr)
    {
        return YomkResponse(YomkResponse::eErr, "context monitor function is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if(m_contexts.find(yMonitor->m_key) == m_contexts.end())
        {
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
    }

    {
        std::unique_lock<std::shared_mutex> monitorLock(m_contextMonitorsMutex);
        m_contextMonitors[yMonitor->m_key].push_back(yMonitor->m_contextMonitorFunc);
    }
    return YomkResponse(YomkResponse::eOk, "set context monitor success");
}
