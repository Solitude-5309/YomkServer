#pragma once
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "YomkBoot.h"
#include "YomkDiag.h"
#include "YomkExport.h"
#include "YomkMsg.h"
#include "YomkMsgs.h"
#include "YomkPayload.h"
#include "YomkServerRef.h"
#include "YomkUrl.h"

// 请求域：request 是刻意保留的基础接口——API 壳屏蔽 URL，但不垄断请求通道，
// 基于 YomkServer 写自定义服务直接依赖本头，链式 API 各 builder 终结 op 亦经此域。
// 链式形态：yomk::server() 域入口 → url() 累积 → request()/status() 同步终结，
// async() 分面（可带回调）→ request() 异步终结。
// 闭环 6（2026-11-06 决议：生命周期亦链式）：原伞头 VERSION/BOOT/SERVER_INFO 12 个
// 自由函数并入本域——async_thread/srv_name/srv_ptr/boot_ptr/fn_url/srv<T> 累积 +
// version/init/instance/shutdown/create/add/remove/run/services/functions/get_info/all
// 无参终结；delService 终结定名 remove（delete 为 C++ 保留字）。
// 无状态设计：全部经 YomkServerRef 快照取当前 server，未初始化处理内聚在本头一层；
// 依赖单向（Boot/Diag/Msgs/Payload/ServerRef/Url/Export），不依赖 YomkApi.h 与任何
// 域头，可独立包含。

/**
 * @brief 服务请求链式 Builder，发起同步/异步服务调用
 *
 * @par 同步请求
 * yomk::server().url(url).msg(msg).request()
 * @li 返回 Response 对象
 * @li .msg(msg) 设置请求消息，YOMK_DEFINE_MSG 消息对象；nullptr 为空消息
 * @li 未初始化时返回 Invalid，并记录诊断信息
 *
 * @par 异步请求
 * yomk::server().url(url).async(func).msg(msg).request()
 * @li 投递消息后立即返回，结果通过回调 func 回调
 * @li func 可为 nullptr：仅投递，不取回应答
 * @li .msg(msg) 可放在 .async() 之前或之后
 *
 * @par 其他接口
 * @li .status()：终结接口，三态收敛，返回 ResStatus
 * @li url：支持服务注册名 / 原始字符串，取自 yomk::url 常量池
 *
 * @par 生命周期与服务管理（闭环 6 并入）
 * @li .async_thread(n).init()：初始化单例并启动内置服务（n 省略 = 0 自动检测）
 * @li .version() / .instance() / .shutdown()：版本串 / 全局快照 / 关闭并释放
 * @li .srv<T>(srvName).create()：创建并注册服务；.srv_name(name).srv_ptr(srv).add()：
 *     注册已有实例（所有权移交框架）；.srv_name(name).remove()：按名注销
 * @li .boot_ptr(cfg).run()：初始化 + 启动编排（before/start/after；cfg 省略 = 仅 init）
 * @li .services() / .srv_name(name).functions() / .fn_url(url).get_info() / .all()：
 *     服务拓扑内省读终结（requireServer 守卫）
 *
 * @warning Builder 持 url 视图，仅限单表达式内联使用，禁止缓存复用，否则悬垂引用！
 */
// 公开面：仅域入口（约定 8：入口暴露、机制内敛，builder 类调用方全程 auto 接收、从不具名）
namespace yomk::detail
{
class RequestBuilder;  // 前置声明，定义见下方机制段
}
namespace yomk
{
// 请求域入口：向当前 server 发起请求；闭环 6 后兼承载生命周期 / 服务管理 / 内身分面
inline detail::RequestBuilder server();  // 按值返回需完整类型，定义在机制段之后
}

// —— 机制段：builder 与守卫（用户无需感知）——
namespace yomk::detail
{
// 异步分面视图：async() 之后允许 msg() 累积与异步终结 request()
class AsyncRequestBuilder
{
    friend class RequestBuilder;
    // url 以 view 透传（请求链全程 string_view 化成果，builder 不产生拷贝）
    std::string_view m_url;
    YomkMsgPtr m_msg;
    YomkResponseFunc m_func;
    AsyncRequestBuilder(std::string_view url, YomkMsgPtr msg, YomkResponseFunc func)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
        : m_url(url), m_msg(std::move(msg)), m_func(std::move(func))
    {
    }

public:
    // 累积请求消息（紧邻终结；省略 = nullptr 空消息）
    AsyncRequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    // 异步请求终结（无参动作）：投递后立即返回，结果经回调送达；未初始化记 diag 后丢弃请求
    void request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return;
        }
        return srv->asyncRequest(m_url, m_msg, m_func);
    }
};
// 守卫：单例未初始化时记日志并返回 false，调用方据此提前返回默认值（仅表面层终结 op 使用；
// 定义须先于 RequestBuilder——其内联终结体直呼本函数）
inline bool requireServer()
{
    if (YomkServerRef::serverSnapshot())
        return true;
    YOMK_DIAG_ERROR("YomkServer is not init");
    return false;
}
// 请求域 builder：url/msg 累积 + request/status 无参同步终结 + async 异步分面
// + 闭环 6 生命周期/服务管理/内身分面（原伞头 12 自由函数并入）
class RequestBuilder
{
    std::string_view m_url;
    YomkMsgPtr m_msg;
    // —— 闭环 6 分面参数（前置累积，约定 5）——
    std::size_t m_asyncThreadCount = 0;  // async_thread() 累积，init 用（0 = 自动检测）
    std::string m_srvName;               // srv_name() 累积，add/remove/functions 共用
    YomkService* m_srvPtr = nullptr;     // srv_ptr() 累积，add 用
    YomkBoot* m_boot = nullptr;          // boot_ptr() 累积，run 用（所有权移交 run）
    std::string m_fnUrl;                 // fn_url() 累积，get_info 用
    // srv<T>() 捕获的实例化工厂：闭包持有服务名，create() 执行
    std::function<int(const std::shared_ptr<YomkServer>&)> m_newSvc;

public:
    // 累积目标 url（自定义服务传注册名/原始串；域 builder 内部取自 yomk::url 常量池）
    RequestBuilder& url(std::string_view u)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_url = u;
        return *this;
    }
    // 累积请求消息（Fluent 约定：参数全部前置累积，终结 op 为无参动作；紧邻终结，省略 = nullptr 空消息）。
    // 命名 msg：实参是打包后的消息对象（YOMK_DEFINE_MSG 生成的 YomkMsg 派生类指针），非裸 payload 结构体
    RequestBuilder& msg(YomkMsgPtr msg)
    {
        m_msg = std::move(msg);
        return *this;
    }
    // 同步请求终结（无参动作）；未初始化返回 Response(Invalid, "YomkServer is not init") 并记 diag
    yomk::payload::Response request()
    {
        auto srv = YomkServerRef::serverSnapshot();
        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return srv->request(m_url, m_msg);
    }
    // 三态收敛终结（无参动作）：失败原因经错误日志承载，返回值仅保留 ResStatus
    // （各域 builder 写路径终结 op 与自定义服务客户端共用）
    yomk::payload::ResStatus status()
    {
        yomk::payload::Response resp = request();
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            YOMK_DIAG_ERROR((resp.m_msg.empty() ? "request failed" : resp.m_msg));
        }
        return resp.m_status;
    }
    // 异步分面：func 为结果回调（nullptr = 仅投递不取回）；已累积的消息携带进分面
    AsyncRequestBuilder async(YomkResponseFunc func = nullptr)
    {
        return AsyncRequestBuilder(m_url, std::move(m_msg), std::move(func));
    }

    // —— 闭环 6 生命周期/服务管理/内身分面（原 YomkAPI 伞头 12 自由函数并入）——
    // 获取框架版本号（一次性读终结，不经请求通道）
    std::string version()
    {
        return YOMKSERVER_VERSION;
    }
    // 累积异步请求线程池大小（init 用，省略 = 0 自动检测）
    RequestBuilder& async_thread(std::size_t asyncThreadCount)
    {
        m_asyncThreadCount = asyncThreadCount;
        return *this;
    }
    // 初始化框架单例并启动内置服务（一次性写终结，幂等 once_flag；atexit 挂销毁兜底）
    void init()
    {
        static std::once_flag initFlag;
        std::call_once(
            initFlag,
            [asyncThreadCount = m_asyncThreadCount]()
            {
                auto server = YomkServer::create(asyncThreadCount);
                server->startService(
                    {"/YomkFunctionPool", "/YomkContext", "/YomkEventLoop", "/YomkLogger", "/YomkServerInfo"});
                YomkServerRef::setServer(server);
                std::atexit([]() { YomkServerRef::holder().destroy(); });
            });
    }
    // 获取全局单例快照，未初始化返回 nullptr（一次性读终结）
    std::shared_ptr<YomkServer> instance()
    {
        return YomkServerRef::serverSnapshot();
    }
    // 关闭服务器并释放所有服务（一次性写终结；未初始化空转）
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
    // 模板化累积服务实例化类型与服务名（create 用；类型擦除进工厂闭包，名随闭包保活）
    template <typename T>
    RequestBuilder& srv(std::string_view srvName = "")  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        std::string name(srvName);
        m_newSvc = [name](const std::shared_ptr<YomkServer>& server) { return server->template newService<T>(name); };
        return *this;
    }
    // 累积服务名（add/remove/functions 共用）
    RequestBuilder& srv_name(std::string_view srvName)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_srvName = srvName;
        return *this;
    }
    // 累积已有服务实例指针（add 用，所有权移交框架）
    RequestBuilder& srv_ptr(YomkService* srv)
    {
        m_srvPtr = srv;
        return *this;
    }
    // 累积启动编排指针（run 用，所有权移交 run）
    RequestBuilder& boot_ptr(YomkBoot* cfg)
    {
        m_boot = cfg;
        return *this;
    }
    // 累积功能函数 url（get_info 用）
    RequestBuilder& fn_url(std::string_view url)  // cppcheck-suppress passedByValue ; string_view 16B trivially copyable, by-value is faster
    {
        m_fnUrl = url;
        return *this;
    }
    // 创建并注册指定类型的服务（写终结，读 srv<T> 累积；未初始化 -1）
    int create()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }
        if (!m_newSvc)
        {
            YOMK_DIAG_ERROR("service type is not set (call srv<T>() first)");
            return -1;
        }
        return m_newSvc(server);
    }
    // 注册已有服务实例（写终结，读 srv_name/srv_ptr 累积；srvName 非空则改名）
    int add()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
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
    // 按名称注销服务（写终结，读 srv_name 累积；delete 为 C++ 保留字，定名 remove）
    int remove()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }
        if (m_srvName.empty())
        {
            YOMK_DIAG_ERROR("service name is empty");
            return -1;
        }
        return server->delService(m_srvName);
    }
    // 初始化并执行启动编排流程（一次性写终结：before→start→after，顺序语义内敛于此；
    // boot_ptr 省略 = 仅 init 返回 0；编排对象所有权由本终结接管）
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
    // 查询所有服务名称（内省读终结）
    yomk::payload::Response services()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return url(yomk::url::srvServices).request();
    }
    // 查询指定服务的全部功能函数（内省读终结，读 srv_name 累积；
    // 返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，与 /function 单查、
    // /all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    yomk::payload::Response functions()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return url(yomk::url::srvFunctions).msg(yomk::msgs::String::shared(m_srvName)).request();
    }
    // 查询指定功能函数元信息（内省读终结，读 fn_url 累积）
    yomk::payload::Response get_info()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return url(yomk::url::srvFunction).msg(yomk::msgs::String::shared(m_fnUrl)).request();
    }
    // 查询框架全量服务及功能函数拓扑（内省读终结）
    yomk::payload::Response all()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return url(yomk::url::srvAll).request();
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
