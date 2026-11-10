#pragma once
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "yomk/boot/YomkBoot.h"
#include "yomk/YomkDiag.h"
#include "yomk/YomkExport.h"
#include "yomk/msg/YomkMsg.h"
#include "yomk/msg/YomkStdMsg.h"  // 标准类型消息目录:请求链常用载荷(String 等)
#include "yomk/server/YomkServerPayload.h"
#include "yomk/server/YomkServerMsg.h"  // 核心载荷消息包:完整消息目录
#include "yomk/server/YomkServerRef.h"

// 服务器域设计:request 是刻意保留的基础接口——API 壳屏蔽 URL 但不垄断请求通道,基于 YomkServer 写自定义服务可直接依赖本头;
// 无状态——终结动作全部经 YomkServerRef 快照取当前 server,未初始化时记诊断并返回失败值,不抛异常;
// 依赖单向 (Boot/Diag/Msg/Payload/ServerRef/Export),可独立包含;拓扑内省读路径见 YomkServerInfoApi.h(yomk::srv_info() 域).

/**
 * @brief Chained builder for the server domain: service request / lifecycle / service management
 *
 * @par Synchronous request
 * yomk::server().url(url).msg(msg).request()
 * @li Returns a Response object, blocks until the service replies
 * @li .msg(msg) sets the request message, a YOMK_DEFINE_MSG message object; nullptr means an empty message
 * @li Returns Invalid if uninitialized, with diagnostics recorded
 *
 * @par Asynchronous request
 * yomk::server().url(url).async(func).msg(msg).request()
 * @li Posts the message and returns immediately, the result is delivered via callback func
 * @li func may be nullptr: post only, no reply is taken back
 * @li .msg(msg) may appear before or after .async()
 *
 * @par Status query
 * @li .status(): three-state terminating variant of the synchronous request, returns ResStatus only
 * @li url: accepts a service registration name or a raw string, taken from the yomk::url constant pool
 *
 * @par Lifecycle and service management
 * @li .async_thread(n).init(): initializes the singleton and starts built-in services (n omitted = 0, auto-detect)
 * @li .version() / .instance() / .shutdown(): version string / global snapshot / shut down and release
 * @li .srv<T>(srvName).create(): creates and registers a service; .srv_name(name).srv_ptr(srv).add():
 *     registers an existing instance (ownership is transferred to the framework); .srv_name(name).remove(): unregisters by name
 * @li .boot_ptr(cfg).run(): init + boot orchestration (before/start/after; cfg omitted = init only)
 *
 * @warning The builder holds a url view, use inline in a single expression only, never cache or reuse the builder, otherwise dangling reference!
 */
// @zh: 服务器域链式 Builder：服务请求 / 生命周期 / 服务管理
// @zh: par 同步请求
// @zh: li 返回 Response 对象，阻塞至服务应答
// @zh: li .msg(msg) 设置请求消息，YOMK_DEFINE_MSG 消息对象；nullptr 为空消息
// @zh: li 未初始化时返回 Invalid，并记录诊断信息
// @zh: par 异步请求
// @zh: li 投递消息后立即返回，结果通过回调 func 送达
// @zh: li func 可为 nullptr：仅投递，不取回应答
// @zh: li .msg(msg) 可放在 .async() 之前或之后
// @zh: par 状态查询
// @zh: li .status()：同步请求的三态收敛终结，仅返回 ResStatus
// @zh: li url：支持服务注册名 / 原始字符串，取自 yomk::url 常量池
// @zh: par 生命周期与服务管理
// @zh: li .async_thread(n).init()：初始化单例并启动内置服务（n 省略 = 0 自动检测）
// @zh: li .version() / .instance() / .shutdown()：版本串 / 全局快照 / 关闭并释放
// @zh: li .srv<T>(srvName).create()：创建并注册服务；.srv_name(name).srv_ptr(srv).add()：注册已有实例（所有权移交框架）；.srv_name(name).remove()：按名注销
// @zh: li .boot_ptr(cfg).run()：初始化 + 启动编排（before/start/after；cfg 省略 = 仅 init）
// @zh: warning Builder 持 url 视图，仅限单表达式内联使用，禁止缓存复用，否则悬垂引用！
// 公开面仅暴露域入口 yomk::server(); builder 机制内敛, 调用方全程以 auto 接收, 从不具名引用类型
namespace yomk::detail
{
class RequestBuilder;  // 前置声明, 定义见下方机制段
}
namespace yomk
{
/**
 * @brief Server domain entry: starting point for request / lifecycle / service management chained calls
 * @return RequestBuilder (by value, for single-expression chained calls only)
 * @note Thread-safe, non-blocking
 */
// @zh: 服务器域入口：发起请求 / 生命周期 / 服务管理链式调用的起点
// @zh: return RequestBuilder（按值，仅用于单表达式链式调用）
// @zh: note 线程安全，非阻塞
inline detail::RequestBuilder server();  // 按值返回需完整类型, 定义在机制段之后
}

// —— 机制段: builder 与守卫 ——
namespace yomk::detail
{
/**
 * @brief Asynchronous request facet: after async(), further calls accumulate via this class and terminate with request()
 * @warning Like RequestBuilder, holds a string_view, use inline in a single expression only, never cache or reuse the builder
 */
// @zh: 异步请求分面：async() 之后经由本类继续累积并以 request() 终结
// @zh: warning 与 RequestBuilder 同为 string_view 持有，仅限单表达式内联使用，禁止缓存复用
class AsyncRequestBuilder
{
    friend class RequestBuilder;
    // url 以 string_view 透传, builder 全程不产生拷贝
    std::string_view m_url;
    YomkMsgPtr m_msg;
    YomkResponseFunc m_func;
    AsyncRequestBuilder(std::string_view url, YomkMsgPtr msg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_url(url), m_msg(std::move(msg)), m_func(std::move(func))
    {
    }

public:
    /**
     * @brief Accumulate the request message
     * @param msg Packed message object, nullptr means an empty message
     * @return Reference to self for chaining
     */
    // @zh: 累积请求消息
    // @zh: msg 打包后的消息对象，nullptr 为空消息
    // @zh: return 自身链式引用
    AsyncRequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    /**
     * @brief Asynchronous request termination: returns immediately after posting, the result is delivered via callback
     * @note Non-blocking, thread-safe
     * @warning If uninitialized, records diagnostics and drops the request, with no return value to indicate it
     */
    // @zh: 异步请求终结：投递后立即返回，结果经回调送达
    // @zh: note 非阻塞，线程安全
    // @zh: warning 未初始化时记录诊断并丢弃请求，无返回值提示
    void request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not initialized");
            return;
        }
        return srv->asyncRequest(m_url, m_msg, m_func);
    }
};

// 守卫: 单例未初始化时记日志并返回 false, 调用方据此提前返回默认值
// (定义须先于 RequestBuilder——其内联终结体直呼本函数)
inline bool requireServer()
{
    if (YomkServerRef::serverSnapshot())
        return true;
    YOMK_DIAG_ERROR("YomkServer is not initialized");
    return false;
}

/**
 * @brief Server domain builder: url/msg accumulation + request/status synchronous termination + async facet
 * + lifecycle / service management terminators
 */
// @zh: 服务器域 builder：url/msg 累积 + request/status 同步终结 + async 异步分面
// @zh: + 生命周期 / 服务管理终结
class RequestBuilder
{
    std::string_view m_url;
    YomkMsgPtr m_msg;
    // 分面参数: 由累积方法写入, 对应终结动作读取
    std::size_t m_asyncThreadCount = 0;  // async_thread() 累积, init 用(0 = 自动检测)
    std::string m_srvName;               // srv_name() 累积, add/remove 共用
    YomkService* m_srvPtr = nullptr;     // srv_ptr() 累积, add 用
    YomkBoot* m_boot = nullptr;          // boot_ptr() 累积, run 用(所有权移交 run)
    // srv<T>() 捕获的实例化工厂: 闭包持有服务名, create() 执行
    std::function<int(const std::shared_ptr<YomkServer>&)> m_newSvc;

public:
    /**
     * @brief Accumulate the target url
     * @param u Service registration name or raw string, preferably from the yomk::url constant pool
     * @return Reference to self for chaining
     * @warning Holds a view without copying; the caller must ensure u stays valid until termination within the same expression
     */
    // @zh: 累积目标 url
    // @zh: u 服务注册名 / 原始串，建议取自 yomk::url 常量池
    // @zh: return 自身链式引用
    // @zh: warning 仅持有视图不拷贝，调用方须保证 u 在同表达式终结前有效
    RequestBuilder& url(std::string_view u)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_url = u;
        return *this;
    }
    /**
     * @brief Accumulate the request message
     * @param msg Packed message object (YomkMsg derived pointer generated by YOMK_DEFINE_MSG), nullptr means an empty message
     * @return Reference to self for chaining
     */
    // @zh: 累积请求消息
    // @zh: msg 打包后的消息对象（YOMK_DEFINE_MSG 生成的 YomkMsg 派生类指针），nullptr 为空消息
    // @zh: return 自身链式引用
    RequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    /**
     * @brief Synchronous request termination
     * @return Response packet; returns Invalid with diagnostics if uninitialized
     * @note Blocks until the service replies; thread-safe
     */
    // @zh: 同步请求终结
    // @zh: return Response 应答包；未初始化返回 Invalid 并记诊断
    // @zh: note 阻塞至服务应答；线程安全
    yomk::payload::Response request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not initialized");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return srv->request(m_url, m_msg);
    }
    /**
     * @brief Synchronous request termination, converged to three states
     * @return ResStatus; the failure reason is carried by diagnostics, the return value keeps the status only
     * @note Blocks until the service replies; thread-safe
     */
    // @zh: 同步请求终结，三态收敛
    // @zh: return ResStatus；失败原因经诊断日志承载，返回值仅保留状态
    // @zh: note 阻塞至服务应答；线程安全
    yomk::payload::ResStatus status()
    {
        yomk::payload::Response resp = request();
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.msg.empty() ? "request failed" : resp.msg));
        }
        return resp.status;
    }
    /**
     * @brief Enter the asynchronous facet
     * @param func Result callback, nullptr = post only without taking the reply back
     * @return Asynchronous facet builder (may continue with .msg() then terminate with .request())
     * @note Accumulated messages are carried into the facet; non-blocking
     */
    // @zh: 切入异步分面
    // @zh: func 结果回调，nullptr = 仅投递不取回
    // @zh: return 异步分面 builder（可继续 .msg() 后 .request() 终结）
    // @zh: note 已累积的消息携带进分面；非阻塞
    AsyncRequestBuilder async(YomkResponseFunc func = nullptr)
    {
        return AsyncRequestBuilder(m_url, std::move(m_msg), std::move(func));
    }

    // —— 生命周期 / 服务管理 ——
    /**
     * @brief Get the framework version string
     * @return Version string (compile-time constant, does not go through the request channel)
     */
    // @zh: 获取框架版本号
    // @zh: return 版本字符串（编译期常量，不经请求通道）
    std::string version()
    {
        return YOMKSERVER_VERSION;
    }
    /**
     * @brief Accumulate the async request thread pool size (used by init)
     * @param asyncThreadCount Thread count, 0 = auto-detect
     * @return Reference to self for chaining
     */
    // @zh: 累积异步请求线程池大小（init 用）
    // @zh: asyncThreadCount 线程数，0 = 自动检测
    // @zh: return 自身链式引用
    RequestBuilder& async_thread(std::size_t asyncThreadCount)
    {
        m_asyncThreadCount = asyncThreadCount;
        return *this;
    }
    /**
     * @brief Initialize the framework singleton and start built-in services
     * @note Idempotent (effective once per process), thread-safe; also registers an atexit destroy fallback
     */
    // @zh: 初始化框架单例并启动内置服务
    // @zh: note 幂等（进程内仅生效一次），线程安全；并挂 atexit 销毁兜底
    void init()
    {
        static std::once_flag initFlag;
        static std::atomic<bool> initDone{false};
        std::call_once(
            initFlag,
            [asyncThreadCount = m_asyncThreadCount]()
            {
                auto server = YomkServer::create(asyncThreadCount);
                server->startService(
                    {"/YomkFunctionPool", "/YomkContext", "/YomkEventLoop", "/YomkLogger", "/YomkServerInfo"});
                YomkServerRef::setServer(server);
                std::atexit([]() { YomkServerRef::holder().destroy(); });
                initDone.store(true, std::memory_order_release);
            });
        // 二调守卫:shutdown 后再 init 是静默 no-op,用户会误以为初始化成功 ——
        // 补 WARN 指出实际状态(幂等语义不变,不支持二次初始化)
        if (initDone.load(std::memory_order_acquire) && !YomkServerRef::serverSnapshot())
        {
            YOMK_DIAG_WARN("server already shut down, init ignored (re-init is not supported)");
        }
    }
    /**
     * @brief Get the global singleton snapshot
     * @return shared_ptr; returns nullptr if uninitialized
     */
    // @zh: 获取全局单例快照
    // @zh: return shared_ptr；未初始化返回 nullptr
    std::shared_ptr<YomkServer> instance()
    {
        return YomkServerRef::serverSnapshot();
    }
    /**
     * @brief Shut down the server and release all services (ownership released along with it)
     * @note No-op if uninitialized
     */
    // @zh: 关闭服务器并释放所有服务（所有权随之释放）
    // @zh: note 未初始化时空转
    void shutdown()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            return;
        }
        server->shutdown();
        YomkServerRef::setServer(nullptr);
    }
    /**
     * @brief Accumulate the service instantiation type and service name (used by create)
     * @tparam T YomkService derived class
     * @param srvName Registration name, empty = keep the default name from T's constructor
     * @return Reference to self for chaining
     */
    // @zh: 累积服务实例化类型与服务名（create 用）
    // @zh: T YomkService 派生类
    // @zh: srvName 注册名，空 = 沿用 T 构造时的默认名
    // @zh: return 自身链式引用
    template <typename T>
    RequestBuilder& srv(std::string_view srvName = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        std::string name(srvName);
        m_newSvc = [name](const std::shared_ptr<YomkServer>& server) { return server->template newService<T>(name); };
        return *this;
    }
    /**
     * @brief Accumulate the service name (shared by add / remove)
     * @param srvName Target service registration name
     * @return Reference to self for chaining
     */
    // @zh: 累积服务名（add / remove 共用）
    // @zh: srvName 目标服务注册名
    // @zh: return 自身链式引用
    RequestBuilder& srv_name(std::string_view srvName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_srvName = srvName;
        return *this;
    }
    /**
     * @brief Accumulate an existing service instance pointer (used by add)
     * @param srv Service instance, ownership is transferred to the framework after registration
     * @return Reference to self for chaining
     */
    // @zh: 累积已有服务实例指针（add 用）
    // @zh: srv 服务实例，注册后所有权移交框架
    // @zh: return 自身链式引用
    RequestBuilder& srv_ptr(YomkService* srv)
    {
        m_srvPtr = srv;
        return *this;
    }
    /**
     * @brief Accumulate the boot orchestration object pointer (used by run)
     * @param cfg Orchestration object, ownership is transferred to run
     * @return Reference to self for chaining
     */
    // @zh: 累积启动编排对象指针（run 用）
    // @zh: cfg 编排对象，所有权移交 run
    // @zh: return 自身链式引用
    RequestBuilder& boot_ptr(YomkBoot* cfg)
    {
        m_boot = cfg;
        return *this;
    }
    /**
     * @brief Create and register the service specified by srv<T>()
     * @return 0 on success; -1 on failure (uninitialized / srv<T>() not called first / init failed, diagnostics recorded in all cases)
     */
    // @zh: 创建并注册 srv<T>() 指定的服务
    // @zh: return 0 成功；-1 失败（未初始化 / 未先调 srv<T> / init 失败，均记诊断）
    int create()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not initialized");
            return -1;
        }
        if (!m_newSvc)
        {
            YOMK_DIAG_ERROR("service type is not set (call srv<T>() first)");
            return -1;
        }
        return m_newSvc(server);
    }
    /**
     * @brief Register the existing service instance specified by srv_ptr()
     * @return 0 on success; -1 on failure (uninitialized / srv_ptr empty / init failed, diagnostics recorded in all cases)
     * @note If a non-empty srv_name was also accumulated, renames the service before registration
     */
    // @zh: 注册 srv_ptr() 指定的已有服务实例
    // @zh: return 0 成功；-1 失败（未初始化 / srv_ptr 为空 / init 失败，均记诊断）
    // @zh: note 若同时累积了非空 srv_name，注册前先改名
    int add()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not initialized");
            return -1;
        }
        if (!m_srvPtr)
        {
            YOMK_DIAG_ERROR("YomkService is null");
            return -1;
        }
        if (!m_srvName.empty())
            m_srvPtr->name(m_srvName);
        return server->addService(m_srvPtr);
    }
    /**
     * @brief Unregister the service by name and release its ownership
     * @return 0 on success; -1 service not found / uninitialized / name empty
     * @note Named remove (delete is a C++ reserved word)
     */
    // @zh: 按名称注销服务并释放其所有权
    // @zh: return 0 成功；-1 服务不存在 / 未初始化 / 名称为空
    // @zh: note 终结定名 remove（delete 为 C++ 保留字）
    int remove()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not initialized");
            return -1;
        }
        if (m_srvName.empty())
        {
            YOMK_DIAG_ERROR("service name is empty");
            return -1;
        }
        return server->delService(m_srvName);
    }
    /**
     * @brief Initialize and run the boot orchestration flow (before -> start -> after)
     * @return 0 on success; non-zero = the return value of the orchestration stage that failed (later stages are not executed)
     * @note If boot_ptr is omitted, only init is executed and 0 is returned; ownership of the orchestration object is taken over by this terminator
     */
    // @zh: 初始化并执行启动编排流程（before → start → after）
    // @zh: return 0 成功；非 0 = 编排某阶段返回值（该阶段起后续不再执行）
    // @zh: note boot_ptr 省略时仅执行 init 并返回 0；编排对象所有权由本终结接管
    int run()
    {
        init();

        std::unique_ptr<YomkBoot> bootGuard(m_boot);
        if (!bootGuard)
            return 0;

        int ret = bootGuard->before();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot before failed!");
            return ret;
        }

        ret = bootGuard->start();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot start failed!");
            return ret;
        }

        ret = bootGuard->after();
        if (ret != 0)
        {
            YOMK_DIAG_ERROR("YomkBoot after failed!");
            return ret;
        }

        return 0;
    }
};
}  // namespace yomk::detail

namespace yomk
{
inline detail::RequestBuilder server()
{
    return detail::RequestBuilder{};
}
}  // namespace yomk
