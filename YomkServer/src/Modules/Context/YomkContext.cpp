#include "YomkContext.h"
#include <iostream>

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
    YomkUnPackPkgResponse(pkg, Context, context);

    if(context->d.m_key.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check Context.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(context->d.m_value == nullptr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "value is empty, please check Context.m_value." << std::endl;
        return YomkResponse(YomkResponse::eErr, "value is empty");
    }

    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if (m_contexts.find(context->d.m_key) != m_contexts.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << context->d.m_key << " already exists, please check Context.m_key." << std::endl;
            return YomkResponse(YomkResponse::eErr, "key already exists");
        }

        m_contexts.emplace(context->d.m_key, context->d.m_value);
    }

    return YomkResponse(YomkResponse::eOk, "create context success");
}

YomkResponse YomkContext::destroy(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, string, str);
    if(!str)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    {
        std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        auto itContext = m_contexts.find(str->d);
        if(itContext == m_contexts.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << str->d << " is not exist, please check key." << std::endl;
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
        m_contexts.erase(itContext); 
    }

    return YomkResponse(YomkResponse::eOk, "destroy context success");
}

YomkResponse YomkContext::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    if(!context)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "Context is empty, please check Context" << std::endl;
        return YomkResponse(YomkResponse::eErr, "Context is empty");
    }

    if(context->d.m_key.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check Context.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty", context->d.m_value);
    }

    std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);

    auto itContext = m_contexts.find(context->d.m_key);
    if(itContext == m_contexts.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << context->d.m_key << " is not exist, please check Context.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is not exist", context->d.m_value);
    }

    return {YomkResponse::eOk, "get context success", itContext->second};
}

YomkResponse YomkContext::set(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, Context, context);
    if(!context)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "Context is empty, please check Context" << std::endl;
        return YomkResponse(YomkResponse::eErr, "Context is empty");
    }
    if(context->d.m_key.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check Context.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }

    std::unique_lock<std::shared_mutex> lockContexts(m_contextsMutex);
    auto itContext = m_contexts.find(context->d.m_key);
    if(itContext == m_contexts.end())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << context->d.m_key << " is not exist, please check Context.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is not exist");
    }

    if(m_checkerEnabled.load())
    {
        std::shared_lock<std::shared_mutex> checkerLock(m_checkersMutex);
        auto itChecker = m_checkers.find(context->d.m_key);
        if(itChecker != m_checkers.end())
        {
            ContextChecker::ECheckStatus checkStatus = itChecker->second(context->d.m_value);
            if(checkStatus == ContextChecker::eReject)
            {
                return YomkResponse(YomkResponse::eErr, "checker reject set context");
            }
        }
    }

    if(itContext->second->name() != context->d.m_value->name())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "context: " << context->d.m_key << " type not match, please check Context.m_value." << std::endl;
        return YomkResponse(YomkResponse::eErr, "context type not match");
    }
    itContext->second = context->d.m_value;
    lockContexts.unlock();

    if(m_monitorEnabled.load())
    {
        std::shared_lock<std::shared_mutex> monitorLock(m_contextMonitorsMutex);
        auto itMonitor = m_contextMonitors.find(context->d.m_key);
        if(itMonitor != m_contextMonitors.end())
        {
            for(auto &monitorFunc : itMonitor->second)
            {
                monitorFunc(context->d);
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
    YomkUnPackPkgResponse(pkg, ContextChecker, checker)
    if(!checker)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "ContextChecker is empty, please check ContextChecker" << std::endl;
        return YomkResponse(YomkResponse::eErr, "ContextChecker is empty");
    }

    if(checker->d.m_key.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check ContextChecker.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(checker->d.m_checkFunc == nullptr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "checkFunc is empty, please check ContextChecker.m_checkFunc." << std::endl;
        return YomkResponse(YomkResponse::eErr, "checkFunc is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if(m_contexts.find(checker->d.m_key) == m_contexts.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << checker->d.m_key << " is not exist, please check ContextChecker.m_key." << std::endl;
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
    }

    {
        std::unique_lock<std::shared_mutex> checkerLock(m_checkersMutex);
        m_checkers[checker->d.m_key] = checker->d.m_checkFunc;
    }

    return YomkResponse(YomkResponse::eOk, "set checker success");
}

YomkResponse YomkContext::setMonitor(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, ContextMonitor, monitor);
    if(!monitor)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "ContextMonitor is empty, please check ContextMonitor" << std::endl;
        return YomkResponse(YomkResponse::eErr, "ContextMonitor is empty");
    }
    if(monitor->d.m_key.empty())
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "key is empty, please check ContextMonitor.m_key." << std::endl;
        return YomkResponse(YomkResponse::eErr, "key is empty");
    }
    if(monitor->d.m_contextMonitorFunc == nullptr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "context monitor function is empty, please check ContextMonitor.m_contextMonitorFunc." << std::endl;
        return YomkResponse(YomkResponse::eErr, "context monitor function is empty");
    }

    {
        std::shared_lock<std::shared_mutex> lockContexts(m_contextsMutex);
        if(m_contexts.find(monitor->d.m_key) == m_contexts.end())
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YomkContext key: " << monitor->d.m_key << " is not exist, please check ContextMonitor.m_key." << std::endl;
            return YomkResponse(YomkResponse::eErr, "key is not exist");
        }
    }

    {
        std::unique_lock<std::shared_mutex> monitorLock(m_contextMonitorsMutex);
        m_contextMonitors[monitor->d.m_key].push_back(monitor->d.m_contextMonitorFunc);
    }
    return YomkResponse(YomkResponse::eOk, "set context monitor success");
}
