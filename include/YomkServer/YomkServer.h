#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "YomkService.h"
#include "YomkExport.h"

class YomkServerPrivate;
/**
 * @brief 服务器实例：服务注册表 + 请求路由（经 instance() 获取, 或由 yomk::server() 域间接使用）
 * @note 实例由 shared_ptr 管理, 禁止直接 delete; 各接口线程安全
 */
class YOMKSERVER_EXPORT YomkServer : public std::enable_shared_from_this<YomkServer>
{
public:
    /**
     * @brief 创建服务器实例（shared_ptr 管理）
     * @param asyncThreadCount 异步请求线程池大小, 0 = 自动检测
     */
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0)
    {
        return std::shared_ptr<YomkServer>(new YomkServer(asyncThreadCount));
    }
    virtual ~YomkServer();

public:
    /**
     * @brief 构造并注册服务
     * @tparam T YomkService 派生类, 以本 server 为参构造
     * @param srvName 注册名, 空 = 沿用 T 的默认名
     * @return 0 成功; -1 失败（T::init() 非 0, 服务已回滚销毁）
     */
    template <typename T>
    int newService(const std::string& srvName = "")
    {
        YomkService* srv = new T(this);

        if (srvName != "")
            srv->name(srvName);

        return addService(srv);
    }

public:
    /**
     * @brief 按名启动内置服务（未知名记诊断并跳过, 恒返回 0）
     */
    int startService(std::vector<std::string> srvNames);
    /**
     * @brief 注册已有服务实例, 所有权移交框架并调用其 init()
     * @return 0 成功; -1 失败（空指针 / init 非 0 或抛异常, 服务回滚注销）
     */
    int addService(YomkService* srv);
    /**
     * @brief 按名注销服务（锁外 deinit 并释放）
     * @return 0 成功; -1 服务不存在
     */
    int delService(const std::string& srvName);
    /**
     * @brief 关闭服务器：停止异步请求池并排空存量任务, 注销全部服务
     * @note 阻塞至在途任务收尾; 幂等
     */
    void shutdown();
    std::vector<std::string> serviceNames();
    std::map<std::string, yomk::payload::FuncInfo> serviceFuncInfos(const std::string& srvName);
    /**
     * @brief 同步请求：url 按 "/服务名/函数名" 路由
     * @return Response; url 非法 / 服务或函数不存在返回 Failed
     * @note 阻塞至服务应答
     */
    yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg = nullptr);
    /**
     * @brief 异步请求：投递到异步请求池后立即返回
     * @param func 结果回调, nullptr = 仅投递不取回
     * @note 非阻塞; 池关闭中投递被拒绝并记诊断; url 非法直接丢弃
     */
    void asyncRequest(std::string_view url, YomkMsgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
