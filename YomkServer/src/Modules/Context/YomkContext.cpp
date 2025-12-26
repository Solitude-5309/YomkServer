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

YomkRespond YomkContext::create(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkRespond(YomkRespond::eErr, "key is empty");
    }
    if(yContext->m_value == nullptr)
    {
        return YomkRespond(YomkRespond::eErr, "value is empty");
    }
    if(m_contexts.find(yContext->m_key) != m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is exist");
    }

    m_contexts[yContext->m_key] = yContext->m_value;

    return YomkRespond(YomkRespond::eOk, "create context success");
}

YomkRespond YomkContext::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);

    if(m_contexts.find(yStr->d) == m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is not exist");
    }

    m_contexts.erase(yStr->d);

    return YomkRespond(YomkRespond::eOk, "destroy context success");
}

YomkRespond YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkRespond(YomkRespond::eErr, "key is empty", yContext->m_value);
    }
    if(m_contexts.find(yContext->m_key) == m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is not exist", yContext->m_value);
    }

    return {YomkRespond::eOk, "get context success", m_contexts[yContext->m_key]};
}

YomkRespond YomkContext::set(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YContext", YContext, yContext);

    if(yContext->m_key.empty())
    {
        return YomkRespond(YomkRespond::eErr, "key is empty");
    }
    if(m_contexts.find(yContext->m_key) == m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is not exist");
    }

    if(m_checkerEnabled)
    {
        if(m_checkers.find(yContext->m_key) != m_checkers.end())
        {
            YContextSetChecker::ECheckStatus checkStatus = m_checkers[yContext->m_key](yContext->m_value);
            if(checkStatus == YContextSetChecker::eReject)
            {
                return YomkRespond(YomkRespond::eErr, "checker reject set context");
            }
        }
    }

    m_contexts[yContext->m_key] = yContext->m_value;

    if(m_monitorEnabled)
    {
        if(m_contextMonitors.find(yContext->m_key) != m_contextMonitors.end())
        {
            for(auto &monitorFunc : m_contextMonitors[yContext->m_key])
            {
                monitorFunc(yContext);
            }
        }
    }

    return YomkRespond(YomkRespond::eOk, "set context success");
}

YomkRespond YomkContext::turnOnChecker(YomkPkgPtr pkg)
{
    m_checkerEnabled = true;
    return YomkRespond(YomkRespond::eOk, "turn on checker success");
}

YomkRespond YomkContext::turnOffChecker(YomkPkgPtr pkg)
{
    m_checkerEnabled = false;
    return YomkRespond(YomkRespond::eOk, "turn off checker success");
}

YomkRespond YomkContext::turnOnMonitor(YomkPkgPtr pkg)
{
    m_monitorEnabled = true;
    return YomkRespond(YomkRespond::eOk, "turn on monitor success");
}

YomkRespond YomkContext::turnOffMonitor(YomkPkgPtr pkg)
{
    m_monitorEnabled = false;
    return YomkRespond(YomkRespond::eOk, "turn off monitor success");
}

YomkRespond YomkContext::setChecker(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YContextSetChecker", YContextSetChecker, yChecker);

    if(yChecker->m_key.empty())
    {
        return YomkRespond(YomkRespond::eErr, "key is empty");
    }
    if(yChecker->m_checkFunc == nullptr)
    {
        return YomkRespond(YomkRespond::eErr, "checkFunc is empty");
    }
    if(m_contexts.find(yChecker->m_key) == m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is not exist");
    }

    m_checkers[yChecker->m_key] = yChecker->m_checkFunc;

    return YomkRespond(YomkRespond::eOk, "set checker success");
}

YomkRespond YomkContext::setMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YContextMonitor", YContextMonitor, yMonitor);

    if(yMonitor->m_key.empty())
    {
        return YomkRespond(YomkRespond::eErr, "key is empty");
    }
    if(yMonitor->m_contextMonitorFunc == nullptr)
    {
        return YomkRespond(YomkRespond::eErr, "context monitor function is empty");
    }
    if(m_contexts.find(yMonitor->m_key) == m_contexts.end())
    {
        return YomkRespond(YomkRespond::eErr, "key is not exist");
    }

    m_contextMonitors[yMonitor->m_key].push_back(yMonitor->m_contextMonitorFunc);

    return YomkRespond(YomkRespond::eOk, "set context monitor success");
}
