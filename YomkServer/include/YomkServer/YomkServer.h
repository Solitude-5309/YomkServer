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
    // 唯一构造入口：YomkServer 必须由 shared_ptr 持有，栈构造/裸 new 在编译期被拒绝。
    // asyncThreadCount 为异步请求池线程数：0 取默认（硬件并发数一半向上取整，兜底 2）；
    // 线程池仅框架内部使用，不对用户暴露。
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

        return addService(srv);
    }

public:
    int startService(std::vector<std::string> srvNames);
    // 注册服务：成功返回 0；服务器/服务为空、服务 init() 失败（自动回滚）返回 -1。
    // 所有权移交：注册成功后框架以 shared_ptr 持有该服务；禁止同一指针重复传入（双重释放）
    int addService(YomkService *srv);
    int delService(const std::string &srvName);
    // 关闭服务器并逐服务调用 deinit()；关闭后不支持重新注册/启动服务（单进程单次初始化）
    void shutdown();
    std::vector<std::string> serviceNames();
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
