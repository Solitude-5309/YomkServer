#include "yomk/server/YomkServer.h"

#include <functional>
#include <iostream>
#include <unordered_map>

#include "../ctx/YomkContext.h"
#include "../event_loop/YomkEventLoop.h"
#include "../fn_pool/YomkFunctionPool.h"
#include "../log/YomkLogger.h"
#include "../srv_info/YomkServerInfo.h"
#include "YomkServerPrivate.h"
#include "yomk/YomkDiag.h"

YomkServer::YomkServer(std::size_t asyncThreadCount) : m_p(new YomkServerPrivate(asyncThreadCount)) {}

YomkServer::~YomkServer()
{
    // 析构时显式关闭:确保异步请求池在 m_p 仍被 shared_ptr 持有时完成排空,
    // 使池中任务通过 weak_ptr.lock() 仍能安全访问 YomkServerPrivate.
    if (m_p)
        m_p->shutdown();
}

// 解析请求 url 为服务名与函数名(/ServiceName/func_name);
// 失败时记录日志并填充错误消息,返回 false(供 request 构造响应,供 asyncRequest 直接丢弃)
static bool parseRequestUrl(std::string_view url, std::string_view& srvName, std::string_view& funcName, std::string& errMsg)
{
    if (url.empty() || url[0] != '/')
    {
        errMsg = "url parse error: " + std::string(url) + ", please start with /";
        YOMK_DIAG_ERROR(errMsg);
        return false;
    }

    size_t posEnd = url.find('/', 1);
    if (posEnd == std::string::npos)
    {
        errMsg = "url parse error: " + std::string(url) + ", not found service name.";
        YOMK_DIAG_ERROR(errMsg);
        return false;
    }

    srvName = url.substr(0, posEnd);
    if (srvName.empty())
    {
        errMsg = "url parse error: srv is empty. ";
        YOMK_DIAG_ERROR(errMsg);
        return false;
    }

    funcName = url.substr(posEnd);
    if (funcName.empty())
    {
        errMsg = "url parse error: function name is empty";
        YOMK_DIAG_ERROR(errMsg);
        return false;
    }

    return true;
}

int YomkServer::startService(std::vector<std::string> srvNames)
{
    static const std::unordered_map<std::string, std::function<YomkService*(YomkServer*)>> serviceCreators = {
        {"/YomkFunctionPool", [](YomkServer* server) { return new YomkFunctionPool(server); }},
        {"/YomkContext", [](YomkServer* server) { return new YomkContext(server); }},
        {"/YomkEventLoop", [](YomkServer* server) { return new YomkEventLoop(server); }},
        {"/YomkLogger", [](YomkServer* server) { return new YomkLogger(server); }},
        {"/YomkServerInfo", [](YomkServer* server) { return new YomkServerInfo(server); }}};

    for (auto& srvName : srvNames)
    {
        auto it = serviceCreators.find(srvName);
        if (it == serviceCreators.end())
        {
            YOMK_DIAG_ERROR("yomk does not support service: " + srvName);
            continue;
        }

        YomkService* srv = it->second(this);
        srv->name(srvName);

        addService(srv);
    }

    return 0;
}

int YomkServer::addService(YomkService* srv)
{
    if (!srv)
    {
        YOMK_DIAG_ERROR("service is null, please check the service.");
        return -1;
    }

    // 先入表取得 shared_ptr 所有权,保证 init() 内 weak_from_this() 有效
    m_p->addService(srv);

    // init 抛异常视同失败:捕获后走统一回滚,避免半初始化服务残留在表
    int initRet = 0;
    try
    {
        initRet = srv->init();
    }
    catch (const std::exception& e)
    {
        YOMK_DIAG_ERROR("service init exception: " + srv->name() + ", what: " + e.what());
        initRet = -1;
    }
    catch (...)
    {
        YOMK_DIAG_ERROR("service init unknown exception: " + srv->name());
        initRet = -1;
    }

    if (initRet != 0)
    {
        YOMK_DIAG_ERROR("service init error: " + srv->name());
        m_p->delService(srv->name());
        return -1;  // 回滚后向调用方传播失败
    }
    return 0;
}

int YomkServer::delService(const std::string& srvName)
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

std::map<std::string, yomk::payload::FuncInfo> YomkServer::serviceFuncInfos(const std::string& srvName)
{
    return m_p->serviceFuncInfos(srvName);
}

yomk::payload::Response YomkServer::request(std::string_view url, YomkMsgPtr pkg)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
{
    std::string_view srvName;
    std::string_view tmpFuncName;
    std::string errMsg;
    if (!parseRequestUrl(url, srvName, tmpFuncName, errMsg))
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, errMsg);
    }

    return m_p->request(srvName, tmpFuncName, pkg);
}

void YomkServer::asyncRequest(std::string_view url, YomkMsgPtr pkg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
{
    std::string_view srvName;
    std::string_view tmpFuncName;
    std::string errMsg;
    if (!parseRequestUrl(url, srvName, tmpFuncName, errMsg))
    {
        return;
    }

    // 投递到异步请求池(有界工作线程),任务生命周期由池队列管理;
    // 使用 weak_ptr 捕获,避免任务持有 shared_ptr 形成 pool->task->YomkServerPrivate
    // 的循环引用;lambda 执行前 lock() 判活,确保 YomkServerPrivate 仍在.
    std::weak_ptr<YomkServerPrivate> weakP = m_p;
    auto callback = [srvNameS = std::string(srvName), tmpFuncNameS = std::string(tmpFuncName), pkg, weakP, func]()
    {
        auto p = weakP.lock();
        if (!p)
            return;
        if (func)
        {
            func(p->request(srvNameS, tmpFuncNameS, pkg));
        }
        else
        {
            p->request(srvNameS, tmpFuncNameS, pkg);
        }
    };
    if (!m_p->postRequestTask(callback))
    {
        YOMK_DIAG_ERROR("server is shutting down, async request ignored.");
    }
}
