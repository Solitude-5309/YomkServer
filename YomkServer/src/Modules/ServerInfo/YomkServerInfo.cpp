#include "YomkServerInfo.h"

YomkServerInfo::YomkServerInfo(YomkServer* server) : YomkService(server)
{
    name("/YomkServerInfo");
    if (server)
    {
        m_weakServer = server->weak_from_this();
    }
}

int YomkServerInfo::init()
{
    installFunc("/services", &YomkServerInfo::listServices);
    installFunc("/functions", &YomkServerInfo::listFunctions, "String");
    installFunc("/function", &YomkServerInfo::functionInfo, "String");
    installFunc("/all", &YomkServerInfo::listAll);
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
    return {YomkResponse::eOk, "ok", yomk::StringArray_::shared(server->serviceNames())};
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

    // 结构化清单：FuncInfo 三字段与 /function 单查、/all 聚合同族（m_funcName 带 / 前缀 url 段）
    std::vector<yomk::FuncInfo> funcs;
    for (auto& iter : server->serviceFuncInfos(ySrvName->d))
    {
        funcs.push_back(iter.second);
    }
    return {YomkResponse::eOk, "ok", yomk::FuncInfoList_::shared(std::move(funcs))};
}

YomkResponse YomkServerInfo::functionInfo(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, yUrl);
    const std::string& url = yUrl->d;
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

    for (auto& name : server->serviceNames())
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
            // 命中：结构化包恒挂，明确字段承载全部返回数据（m_msg 恒 "ok"，数据走 m_data）；
            // 复用 yomk::FuncInfo 与 FunctionPool /name 单查端点同族
            yomk::FuncInfo info;
            info.m_funcName = funcName;
            info.m_msgName = iter->second.m_msgName;
            info.m_description = iter->second.m_description;
            return {YomkResponse::eOk, "ok", yomk::FuncInfo_::shared(info)};
        }
    }

    YOMK_ERR_POS_LOG("service not found -> " + srvName);
    return {YomkResponse::eNo, "service not found: " + srvName};
}

YomkResponse YomkServerInfo::listAll(YomkPkgPtr pkg)
{
    (void)pkg;
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_ERR_POS_LOG("server has been destroyed, list all ignored.");
        return {YomkResponse::eNo, "server has been destroyed"};
    }

    // 同现实现取数锁序：serviceNames() 一次，再逐服务 serviceFuncInfos()；
    // FuncInfo 三字段与 /function 单查同族整体拷贝（m_funcName 为带 / 前缀 url 段）
    std::vector<yomk::ServerServiceInfo> infos;
    for (auto& srvName : server->serviceNames())
    {
        yomk::ServerServiceInfo srv;
        srv.m_serviceName = srvName;
        auto funcInfos = server->serviceFuncInfos(srvName);
        srv.m_funcs.reserve(funcInfos.size());
        for (auto& iter : funcInfos)
        {
            srv.m_funcs.push_back(iter.second);
        }
        infos.push_back(std::move(srv));
    }
    // m_msg 恒 "ok"，数据走 m_data（结构化层级包：服务 → 函数元信息）
    return {YomkResponse::eOk, "ok", yomk::ServerServiceInfoList_::shared(std::move(infos))};
}
