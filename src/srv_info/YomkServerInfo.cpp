#include "YomkServerInfo.h"
#include "yomk/YomkDiag.h"
#include "yomk/srv_info/YomkServerInfoMsg.h"

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

yomk::payload::Response YomkServerInfo::listServices(YomkMsgPtr pkg)
{
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, list services ignored");
        return {yomk::payload::ResStatus::Failed, "server has been destroyed"};
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(server->serviceNames())};
}

yomk::payload::Response YomkServerInfo::listFunctions(YomkMsgPtr pkg)
{
    auto ySrvName = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!ySrvName)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    if (ySrvName->d.empty())
    {
        YOMK_DIAG_WARN("srvName is empty, please check String.d");
        return {yomk::payload::ResStatus::Invalid, "srvName is empty"};
    }

    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, list functions ignored");
        return {yomk::payload::ResStatus::Failed, "server has been destroyed"};
    }

    // 结构化清单:FuncInfo 三字段与 /function 单查,/all 聚合同族(m_funcName 带 / 前缀 url 段)
    std::vector<yomk::payload::FuncInfo> funcs;
    for (auto& iter : server->serviceFuncInfos(ySrvName->d))
    {
        funcs.push_back(iter.second);
    }
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::FuncInfoList::shared(std::move(funcs))};
}

yomk::payload::Response YomkServerInfo::functionInfo(YomkMsgPtr pkg)
{
    auto yUrl = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!yUrl)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is not String, got '" + yomk::msgs::typeOf(pkg) + "\'"};
    }
    const std::string& url = yUrl->d;
    if (url.empty() || url[0] != '/')
    {
        YOMK_DIAG_WARN("url parse error: " + url + ", please start with /");
        return {yomk::payload::ResStatus::Failed, "url parse error: " + url + ", please start with /"};
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        YOMK_DIAG_WARN("url parse error: " + url + ", no function name");
        return {yomk::payload::ResStatus::Failed, "url parse error: " + url + ", no function name"};
    }

    std::string srvName = url.substr(0, posEnd);
    std::string funcName = url.substr(posEnd);

    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, function info ignored");
        return {yomk::payload::ResStatus::Failed, "server has been destroyed"};
    }

    for (auto& name : server->serviceNames())
    {
        if (name == srvName)
        {
            auto infos = server->serviceFuncInfos(srvName);
            auto iter = infos.find(funcName);
            if (iter == infos.end())
            {
                return {yomk::payload::ResStatus::Failed, "function not found: " + url};
            }
            // 命中:复用 yomk::payload::FuncInfo 与 FunctionPool /name 单查端点同族
            yomk::payload::FuncInfo info;
            info.m_funcName = funcName;
            info.m_msgName = iter->second.m_msgName;
            info.m_description = iter->second.m_description;
            return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::FuncInfo::shared(info)};
        }
    }

    YOMK_DIAG_WARN("service not found: " + srvName);
    return {yomk::payload::ResStatus::Failed, "service not found: " + srvName};
}

yomk::payload::Response YomkServerInfo::listAll(YomkMsgPtr pkg)
{
    (void)pkg;
    auto server = m_weakServer.lock();
    if (!server)
    {
        YOMK_DIAG_ERROR("server has been destroyed, list all ignored");
        return {yomk::payload::ResStatus::Failed, "server has been destroyed"};
    }

    // 取数锁序:serviceNames() 一次,再逐服务 serviceFuncInfos();
    // FuncInfo 三字段与 /function 单查同族整体拷贝(m_funcName 为带 / 前缀 url 段)
    std::vector<yomk::payload::ServerServiceInfo> infos;
    for (auto& srvName : server->serviceNames())
    {
        yomk::payload::ServerServiceInfo srv;
        srv.m_serviceName = srvName;
        auto funcInfos = server->serviceFuncInfos(srvName);
        srv.m_funcs.reserve(funcInfos.size());
        for (auto& iter : funcInfos)
        {
            srv.m_funcs.push_back(iter.second);
        }
        infos.push_back(std::move(srv));
    }
    // 结构化层级包:服务 → 函数元信息
    return {yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::ServerServiceInfoList::shared(std::move(infos))};
}
