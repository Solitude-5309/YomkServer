#include "yomk/server/YomkService.h"

#include <iostream>

#include "yomk/server/YomkServer.h"
#include "YomkServicePrivate.h"
#include "yomk/YomkDiag.h"

YomkService::YomkService(YomkServer* server) : m_p(nullptr)
{
    if (server)
    {
        std::weak_ptr<YomkServer> weakServer = server->weak_from_this();
        if (weakServer.expired())
        {
            YOMK_DIAG_ERROR("server is not managed by shared_ptr, weak bind failed");
        }
        m_p.reset(new YomkServicePrivate(weakServer));
    }
    else
    {
        YOMK_DIAG_ERROR("server is null");
    }
}

void YomkService::name(const std::string& name)
{
    if (name.empty())
    {
        YOMK_DIAG_ERROR("name is empty, set name failed, use default name");
        return;
    }

    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return;
    }
    m_p->name(name);
}

bool YomkService::isDeleted() const
{
    if (!m_p)
    {
        return true;  // 无实现体视同已失效，弱绑定回调一律丢弃
    }
    return m_p->deleted();
}

std::string YomkService::name()
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return "";
    }
    return m_p->name();
}

void YomkService::installFunc(const std::string& funcName, YomkServiceFunc func, const std::string& msgName,
                              const std::string& description)
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return;
    }
    m_p->installFunc(funcName, func, msgName, description);
}

std::map<std::string, yomk::payload::FuncInfo> YomkService::funcInfos()
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return {};
    }
    return m_p->funcInfos();
}

yomk::payload::Response YomkService::invoke(std::string_view funcName, YomkMsgPtr pkg)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "service is null");
    }
    return m_p->invoke(funcName, pkg);
}

yomk::payload::Response YomkService::request(std::string_view url, YomkMsgPtr pkg)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "service is null");
    }
    return m_p->request(url, pkg);
}

void YomkService::asyncRequest(std::string_view url, YomkMsgPtr pkg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
{
    if (!m_p)
    {
        YOMK_DIAG_ERROR("service is null, please check service");
        return;
    }
    m_p->asyncRequest(url, pkg, func);
}
