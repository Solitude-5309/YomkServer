#include "YomkServerInfo.h"

YomkServerInfo::YomkServerInfo(YomkServer *server)
    : YomkService(server)
{
    name("/YomkServerInfo");
    if (server)
    {
        m_weakServer = server->weak_from_this();
    }
}

int YomkServerInfo::init()
{
    YomkInstallFunc("/services", YomkServerInfo::listServices);
    YomkInstallFunc("/functions", YomkServerInfo::listFunctions, String);
    YomkInstallFunc("/function", YomkServerInfo::functionInfo, String);
    YomkInstallFunc("/all", YomkServerInfo::listAll);
    return 0;
}

YomkResponse YomkServerInfo::listServices(YomkPkgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, list services ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, server->serviceNames())};
}

YomkResponse YomkServerInfo::listFunctions(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, ySrvName);
    if (ySrvName->d.empty())
    {
        YOMK_ERR_POS_LOG("srvName is empty, please check String.d");
        return {YomkResponse::eInvalid, "srvName is empty"};
    }

    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, list functions ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    std::vector<std::string> lines;
    for (auto &iter : server->serviceFuncInfos(ySrvName->d))
    {
        lines.push_back(iter.second.m_funcName + (iter.second.m_msgName.empty() ? "" : " [" + iter.second.m_msgName + "]"));
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}

YomkResponse YomkServerInfo::functionInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yUrl);
    const std::string &url = yUrl->d;
    if (url.empty() || url[0] != '/')
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", please start with /");
        return {YomkResponse::eNo, "url parse error: " + url + ", please start with /"};
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        YOMK_ERR_POS_LOG("url parse error: " + url + ", not found function name.");
        return {YomkResponse::eNo, "url parse error: " + url + ", not found function name."};
    }

    std::string srvName = url.substr(0, posEnd);
    std::string funcName = url.substr(posEnd);

    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, function info ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    for (auto &name : server->serviceNames())
    {
        if (name == srvName)
        {
            auto infos = server->serviceFuncInfos(srvName);
            auto iter = infos.find(funcName);
            if (iter == infos.end())
            {
                YOMK_ERR_POS_LOG("function not found -> " + url);
                return {YomkResponse::eNo, "function not found: " + url};
            }
            return {YomkResponse::eOk, iter->second.m_msgName};
        }
    }

    YOMK_ERR_POS_LOG("service not found -> " + srvName);
    return {YomkResponse::eNo, "service not found: " + srvName};
}

YomkResponse YomkServerInfo::listAll(YomkPkgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, list all ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    std::vector<std::string> lines;
    for (auto &srvName : server->serviceNames())
    {
        lines.push_back(srvName);
        for (auto &iter : server->serviceFuncInfos(srvName))
        {
            lines.push_back("  " + srvName + iter.second.m_funcName + (iter.second.m_msgName.empty() ? "" : " [" + iter.second.m_msgName + "]"));
        }
    }
    return {YomkResponse::eOk, "ok", YomkMkPtr(StringArray, lines)};
}
