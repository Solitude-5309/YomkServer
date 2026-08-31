#include "YomkServerPrivate.h"
#include "YomkServer.h"
#include <iostream>
#include <mutex>

void YomkServerPrivate::addService(YomkService *srv)
{
    if (!srv)
    {
        YOMK_ERR_POS_LOG("service is null, please check the service.");
        return;
    }
    std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
    if (m_serviceMap.find(srv->name()) != m_serviceMap.end())
    {
        YOMK_ERR_POS_LOG("service already exists -> " + srv->name() + ", update to current service");
    }
    m_serviceMap[srv->name()].reset(srv);
}

int YomkServerPrivate::delService(const std::string &srvName)
{
    std::shared_ptr<YomkService> srv;
    {
        std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            YOMK_ERR_POS_LOG("service not found, can not delete -> " + srvName);
            return -1;
        }
        srv = std::move(iter->second);
        m_serviceMap.erase(iter);
    }

    // 锁外执行 deinit 与析构：在途请求持有的 shared_ptr 副本保证不会与 invoke 并发析构
    srv->deinit();
    return 0;
}

void YomkServerPrivate::shutdown()
{
    // 幂等：重复调用直接返回，析构兜底与显式 YOMK_SHUTDOWN 不会双重 deinit
    if (m_shutdown.exchange(true))
    {
        return;
    }

    // 先停异步任务池（拒新 -> 排空 -> join）：服务 deinit 前保证无任何异步任务在执行，
    // 消除回调打到已 deinit 服务的功能性竞态；排空中嵌套发起的 asyncRequest 因 m_shutdown 已置位被拒绝，不会无限排空
    m_asyncPool.stop();

    std::vector<std::shared_ptr<YomkService>> srvs;
    {
        std::unique_lock<std::shared_mutex> lock(m_serviceMapMtx);
        srvs.reserve(m_serviceMap.size());
        for (auto &iter : m_serviceMap)
        {
            srvs.push_back(std::move(iter.second));
        }
        m_serviceMap.clear();
    }

    // 锁外逐个 deinit，与 delService 保持一致的锁外清理模式，避免阻塞其他请求路径
    for (auto &srv : srvs)
    {
        srv->deinit();
    }
}

bool YomkServerPrivate::postAsyncTask(std::function<void()> task)
{
    // 服务器已关闭直接拒绝，池已停止时 post 同样返回 false
    if (m_shutdown.load())
    {
        return false;
    }
    return m_asyncPool.post(std::move(task));
}

YomkResponse YomkServerPrivate::request(const std::string &srvName, const std::string &funcName, YomkPkgPtr pkg)
{
    std::shared_ptr<YomkService> srv;
    {
        std::shared_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            YOMK_ERR_POS_LOG("service not found. " + srvName + ", please start the service.");
            return YomkResponse(YomkResponse::eNo, "service not found: " + srvName);
        }
        srv = iter->second;
    }
    return srv->invoke(funcName, pkg);
}

std::vector<std::string> YomkServerPrivate::serviceNames()
{
    std::vector<std::string> names;
    std::shared_lock<std::shared_mutex> lock(m_serviceMapMtx);
    for (auto &iter : m_serviceMap)
    {
        names.push_back(iter.first);
    }
    return names;
}

std::map<std::string, YomkFuncInfo> YomkServerPrivate::serviceFuncInfos(const std::string &srvName)
{
    std::shared_ptr<YomkService> srv;
    {
        std::shared_lock<std::shared_mutex> lock(m_serviceMapMtx);
        auto iter = m_serviceMap.find(srvName);
        if (iter == m_serviceMap.end())
        {
            YOMK_ERR_POS_LOG("service not found. " + srvName + ", please start the service.");
            return {};
        }
        srv = iter->second;
    }
    return srv->funcInfos();
}
