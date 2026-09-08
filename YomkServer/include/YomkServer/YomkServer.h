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
    // 创建 YomkServer 实例（由 shared_ptr 管理），asyncThreadCount 为异步请求线程池大小
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0)
    {
        return std::shared_ptr<YomkServer>(new YomkServer(asyncThreadCount));
    }
    virtual ~YomkServer();

public:
    // 创建并注册指定类型的服务
    template <typename T>
    int newService(const std::string &srvName = "")
    {
        YomkService *srv = new T(this);

        if (srvName != "")
            srv->name(srvName);

        return addService(srv);
    }

public:
    // 启动指定服务
    int startService(std::vector<std::string> srvNames);
    // 注册服务实例（所有权移交给框架）
    int addService(YomkService *srv);
    // 按服务名注销服务
    int delService(const std::string &srvName);
    // 关闭服务器并停止所有服务
    void shutdown();
    // 获取全部服务名称列表
    std::vector<std::string> serviceNames();
    // 获取指定服务的功能函数元信息
    std::map<std::string, YomkFuncInfo> serviceFuncInfos(const std::string &srvName);
    // 同步请求服务功能函数（URL: "/服务名/函数名"）
    YomkResponse request(const std::string &url, YomkPkgPtr pkg = nullptr);
    // 异步请求服务功能函数
    void asyncRequest(const std::string &url, YomkPkgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
