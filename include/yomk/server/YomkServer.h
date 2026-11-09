#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "yomk/server/YomkService.h"
#include "yomk/YomkExport.h"
#include "yomk/srv_info/YomkServerInfoPayload.h"

class YomkServerPrivate;
/**
 * @brief Server instance: service registry + request routing (obtained via instance(), or used indirectly through the yomk::server() domain)
 * @note Instances are managed by shared_ptr, never delete directly; all interfaces are thread-safe
 */
// @zh: 服务器实例：服务注册表 + 请求路由（经 instance() 获取，或由 yomk::server() 域间接使用）
// @zh: note 实例由 shared_ptr 管理，禁止直接 delete；各接口线程安全
class YOMK_EXPORT YomkServer : public std::enable_shared_from_this<YomkServer>
{
public:
    /**
     * @brief Create a server instance (managed by shared_ptr)
     * @param asyncThreadCount Async request thread pool size, 0 = auto-detect
     */
    // @zh: 创建服务器实例（shared_ptr 管理）
    // @zh: asyncThreadCount 异步请求线程池大小，0 = 自动检测
    static std::shared_ptr<YomkServer> create(std::size_t asyncThreadCount = 0)
    {
        return std::shared_ptr<YomkServer>(new YomkServer(asyncThreadCount));
    }
    virtual ~YomkServer();

public:
    /**
     * @brief Construct and register a service
     * @tparam T YomkService derived class, constructed with this server as argument
     * @param srvName Registration name, empty = keep T's default name
     * @return 0 on success; -1 on failure (T::init() returned non-zero, the service has been rolled back and destroyed)
     */
    // @zh: 构造并注册服务
    // @zh: T YomkService 派生类，以本 server 为参构造
    // @zh: srvName 注册名，空 = 沿用 T 的默认名
    // @zh: return 0 成功；-1 失败（T::init() 非 0，服务已回滚销毁）
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
     * @brief Start built-in services by name (unknown names are recorded as diagnostics and skipped, always returns 0)
     */
    // @zh: 按名启动内置服务（未知名记诊断并跳过，恒返回 0）
    int startService(std::vector<std::string> srvNames);
    /**
     * @brief Register an existing service instance, ownership is transferred to the framework and its init() is called
     * @return 0 on success; -1 on failure (null pointer / init returned non-zero or threw, the service is rolled back and unregistered)
     */
    // @zh: 注册已有服务实例，所有权移交框架并调用其 init()
    // @zh: return 0 成功；-1 失败（空指针 / init 非 0 或抛异常，服务回滚注销）
    int addService(YomkService* srv);
    /**
     * @brief Unregister a service by name (deinit and release outside the lock)
     * @return 0 on success; -1 service not found
     */
    // @zh: 按名注销服务（锁外 deinit 并释放）
    // @zh: return 0 成功；-1 服务不存在
    int delService(const std::string& srvName);
    /**
     * @brief Shut down the server: stop the async request pool and drain pending tasks, unregister all services
     * @note Blocks until in-flight tasks finish; idempotent
     */
    // @zh: 关闭服务器：停止异步请求池并排空存量任务，注销全部服务
    // @zh: note 阻塞至在途任务收尾；幂等
    void shutdown();
    /** @brief All service names (registry lexicographic order) */
    // @zh: 全部服务名（注册表字典序）
    std::vector<std::string> serviceNames();
    /** @brief Function metadata of the given service; returns an empty list if the service does not exist */
    // @zh: 指定服务的功能函数元信息；服务不存在返回空表
    std::map<std::string, yomk::payload::FuncInfo> serviceFuncInfos(const std::string& srvName);
    /**
     * @brief Synchronous request: routes by url in the form "/service name/function name"
     * @return Response; returns Failed if the url is invalid or the service/function does not exist
     * @note Blocks until the service replies
     */
    // @zh: 同步请求：url 按 "/服务名/函数名" 路由
    // @zh: return Response；url 非法 / 服务或函数不存在返回 Failed
    // @zh: note 阻塞至服务应答
    yomk::payload::Response request(std::string_view url, YomkMsgPtr pkg = nullptr);
    /**
     * @brief Asynchronous request: posted to the async request pool and returns immediately
     * @param func Result callback, nullptr = post only without taking the reply back
     * @note Non-blocking; posting is rejected with diagnostics while the pool is shutting down; invalid urls are dropped directly
     */
    // @zh: 异步请求：投递到异步请求池后立即返回
    // @zh: func 结果回调，nullptr = 仅投递不取回
    // @zh: note 非阻塞；池关闭中投递被拒绝并记诊断；url 非法直接丢弃
    void asyncRequest(std::string_view url, YomkMsgPtr pkg = nullptr, YomkResponseFunc func = nullptr);

private:
    YomkServer(std::size_t asyncThreadCount);
    std::shared_ptr<YomkServerPrivate> m_p;
};
