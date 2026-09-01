#pragma once
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <vector>

#include "YomkService.h"

class YomkServerPrivate;
class YOMKSERVER_EXPORT YomkServer : public std::enable_shared_from_this<YomkServer>
{
public:
    // 唯一构造入口：YomkServer 必须由 shared_ptr 持有（enable_shared_from_this 契约，
    // YomkService 构造依赖 weak_from_this()）；栈构造/裸 new 在编译期被拒绝；
    // asyncThreadCount 为异步线程池线程数：0 取默认（硬件并发数一半向上取整，兜底 2）
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0)
    {
        return std::shared_ptr<YomkServer>(new YomkServer(asyncThreadCount));
    }
    virtual ~YomkServer() {}

public:
    template <typename T>
    int newService(const std::string &srvName = "")
    {
        YomkService *srv = new T(this);

        if (srvName != "")
            srv->name(srvName);

        return addService(srv); // 传播注册失败（如 init() 返回非 0）
    }

public:
    int startService(std::vector<std::string> srvNames);
    // 注册服务：成功返回 0；服务器/服务为空、服务 init() 失败（自动回滚）返回 -1
    int addService(YomkService *srv);
    int delService(const std::string &srvName);
    // 关闭后不支持重新注册/启动服务（框架单例依赖 std::call_once 初始化）
    void shutdown();
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);
    // 投递任意任务到内部异步线程池：成功返回 true；服务器为空/已关闭/池已停止返回 false，由调用方记日志丢弃
    bool postAsyncTask(std::function<void()> task);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
