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

YomkServer::~YomkServer()
{
    // 析构时显式关闭：确保异步请求池在 m_p 仍被 shared_ptr 持有时完成排空，
    // 使池中任务通过 weak_ptr.lock() 仍能安全访问 YomkServerPrivate。
    if (m_p)
        m_p->shutdown();
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

    // init 抛异常视同失败：捕获后走统一回滚，避免半初始化服务残留在表
    int initRet = 0;
    try
    {
        initRet = srv->init();
    }
    catch (const std::exception &e)
    {
        YOMK_ERR_POS_LOG("service init exception: " + srv->name() + ", what: " + e.what());
        initRet = -1;
    }
    catch (...)
    {
        YOMK_ERR_POS_LOG("service init unknown exception: " + srv->name());
        initRet = -1;
    }

    if (initRet != 0)
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
    // 使用 weak_ptr 捕获，避免任务持有 shared_ptr 形成 pool->task->YomkServerPrivate
    // 的循环引用；lambda 执行前 lock() 判活，确保 YomkServerPrivate 仍在。
    std::weak_ptr<YomkServerPrivate> weakP = m_p;
    auto callback = [srvName, tmpFuncName, pkg, weakP, func]()
    {
        auto p = weakP.lock();
        if (!p)
            return;
        if (func)
        {
            func(p->request(srvName, tmpFuncName, pkg));
        }
        else
        {
            p->request(srvName, tmpFuncName, pkg);
        }
    };
    if (!m_p->postRequestTask(callback))
    {
        YOMK_ERR_POS_LOG("server is shutting down, async request ignored.");
    }
}
