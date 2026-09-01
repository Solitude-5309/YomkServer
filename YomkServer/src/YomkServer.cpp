#include "YomkServer.h"
#include "YomkServerPrivate.h"
#include <iostream>
#include <unordered_map>
#include <functional>
#include "Modules/FunctionPool/YomkFunctionPool.h"
#include "Modules/Context/YomkContext.h"
#include "Modules/EventLoop/YomkEventLoop.h"
#include "Modules/Logger/YomkLogger.h"
#include "Modules/ServerInfo/YomkServerInfo.h"

YomkServer::YomkServer(std::size_t asyncThreadCount)
    : m_p(new YomkServerPrivate(asyncThreadCount))
{
}

// 解析请求 url 为服务名与函数名（/ServiceName/func_name）；
// 失败时记录日志并填充错误消息，返回 false（供 request 构造响应，供 asyncRequest 直接丢弃）
static bool parseRequestUrl(const std::string &url, std::string &srvName, std::string &funcName, std::string &errMsg)
{
    if (url.empty() || url[0] != '/')
    {
        errMsg = "url parse error: " + url + ", please start with /";
        YOMK_ERR_POS_LOG(errMsg);
        return false;
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        errMsg = "url parse error: " + url + ", not found service name.";
        YOMK_ERR_POS_LOG(errMsg);
        return false;
    }

    srvName = url.substr(0, posEnd);
    if (srvName.empty())
    {
        errMsg = "url parse error: srv is empty. ";
        YOMK_ERR_POS_LOG(errMsg);
        return false;
    }

    funcName = url.substr(posEnd);
    if (funcName.empty())
    {
        errMsg = "url parse error: function name is empty";
        YOMK_ERR_POS_LOG(errMsg);
        return false;
    }

    return true;
}

int YomkServer::startService(std::vector<std::string> srvNames)
{
    static const std::unordered_map<std::string, std::function<YomkService *(YomkServer *)>> serviceCreators = {
        {"/YomkFunctionPool", [](YomkServer *server)
         { return new YomkFunctionPool(server); }},
        {"/YomkContext", [](YomkServer *server)
         { return new YomkContext(server); }},
        {"/YomkEventLoop", [](YomkServer *server)
         { return new YomkEventLoop(server); }},
        {"/YomkLogger", [](YomkServer *server)
         { return new YomkLogger(server); }},
        {"/YomkServerInfo", [](YomkServer *server)
         { return new YomkServerInfo(server); }}};

    for (auto &srvName : srvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {
            YOMK_ERR_POS_LOG("yomk does not support service: " + srvName);
            continue;
        }

        YomkService *srv = it->second(this);
        srv->name(srvName);

        addService(srv);
    }

    return 0;
}

int YomkServer::addService(YomkService *srv)
{
    if (!srv)
    {
        YOMK_ERR_POS_LOG("service is null, please check the service.");
        return -1;
    }

    // 先入表取得 shared_ptr 所有权，保证 init() 内 weak_from_this() 有效
    m_p->addService(srv);

    if (srv->init() != 0)
    {
        YOMK_ERR_POS_LOG("service init error: " + srv->name());
        m_p->delService(srv->name());
        return -1; // 回滚后向调用方传播失败
    }
    return 0;
}

int YomkServer::delService(const std::string &srvName)
{
    return m_p->delService(srvName);
}

void YomkServer::shutdown()
{
    m_p->shutdown();
}

std::vector<std::string> YomkServer::serviceNames()
{
    return m_p->serviceNames();
}

std::map<std::string, YomkFuncInfo> YomkServer::serviceFuncInfos(const std::string &srvName)
{
    return m_p->serviceFuncInfos(srvName);
}

YomkResponse YomkServer::request(const std::string &url, YomkPkgPtr pkg)
{
    std::string srvName;
    std::string tmpFuncName;
    std::string errMsg;
    if (!parseRequestUrl(url, srvName, tmpFuncName, errMsg))
    {
        return YomkResponse(YomkResponse::eNo, errMsg);
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(const std::string &url, YomkPkgPtr pkg, YomkResponseFunc func)
{
    std::string srvName;
    std::string tmpFuncName;
    std::string errMsg;
    if (!parseRequestUrl(url, srvName, tmpFuncName, errMsg))
    {
        return;
    }

    // 投递到异步请求池（有界工作线程），任务生命周期由池队列管理；
    // 按值捕获 shared_ptr，shutdown 排空阶段任务仍可安全执行，关闭后投递被拒绝
    std::shared_ptr<YomkServerPrivate> p = m_p;
    if (!m_p->postRequestTask([srvName, tmpFuncName, pkg, p, func]()
                              {
        if(func)
        {
            func(p->request(srvName, tmpFuncName, pkg));
        }
        else
        {
            p->request(srvName, tmpFuncName, pkg);
        } }))
    {
        YOMK_ERR_POS_LOG("server is shutting down, async request ignored.");
    }
}
