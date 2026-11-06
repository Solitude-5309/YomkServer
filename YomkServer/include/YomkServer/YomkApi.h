#pragma once
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "YomkBoot.h"
#include "YomkExport.h"
#include "YomkMsgs.h"
#include "YomkContextApi.h"
#include "YomkEventLoopApi.h"
#include "YomkFunctionPoolApi.h"
#include "YomkLogApi.h"
#include "YomkRequest.h"
#include "YomkServer.h"
#include "YomkServerRef.h"
#include "YomkService.h"
#include "YomkDiag.h"
#include "YomkUrl.h"

// YomkServer 全局统一入口与便捷调用类
class YOMKSERVER_EXPORT YomkAPI
{
    // VERSION_API
public:
    // 获取框架版本号
    static std::string version();
    // BOOT_API
public:
    // 初始化框架单例并启动内置服务，asyncThreadCount 为异步请求线程池大小（0 表示自动检测）
    static void init(std::size_t asyncThreadCount = 0)
    {
        static std::once_flag initFlag;
        std::call_once(
            initFlag,
            [asyncThreadCount]()
            {
                auto server = YomkServer::create(asyncThreadCount);
                server->startService(
                    {"/YomkFunctionPool", "/YomkContext", "/YomkEventLoop", "/YomkLogger", "/YomkServerInfo"});
                YomkServerRef::setServer(server);
                std::atexit([]() { YomkServerRef::holder().destroy(); });
            });
    }
    // 获取全局单例，未初始化返回 nullptr
    static std::shared_ptr<YomkServer> serverInstance() { return YomkServerRef::serverSnapshot(); }
    // 关闭服务器并释放所有服务
    static void shutdown()
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            return;
        }
        server->shutdown();
        YomkServerRef::setServer(nullptr);
    }
    // 创建并注册指定类型的服务
    template <typename T>
    static int newService(const std::string& srvName = "")
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }
        return server->newService<T>(srvName);
    }
    // 注册已有服务实例（所有权移交给框架）
    static int addService(YomkService* srv = nullptr, const std::string& srvName = "")
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }

        if (!srv)
        {
            YOMK_DIAG_ERROR("YomkService is null");
            return -1;
        }

        if (srvName != "")
            srv->name(srvName);

        return server->addService(srv);
    }
    // 按名称注销服务
    static int delService(const std::string& srvName = "")
    {
        auto server = YomkServerRef::serverSnapshot();
        if (!server)
        {
            YOMK_DIAG_ERROR("YomkServer is not init");
            return -1;
        }

        if (srvName.empty())
        {
            YOMK_DIAG_ERROR("service name is empty");
            return -1;
        }

        return server->delService(srvName);
    }
    // 初始化并执行启动编排流程
    static int boot(YomkBoot* cfg = nullptr)
    {
        init();

        std::unique_ptr<YomkBoot> bootGuard(cfg);
        if (!bootGuard)
            return 0;

        int ret = 0;
        ret = bootGuard->before();
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
    // REQ_API：已整体迁移至 YomkRequest.h 的 yomk::server() 请求域（url/request/status/async），
    // 旧 request/asyncRequest 垫片与全部调用点已切换链式写法
    // LOG_API
    // LOG_API：已整体迁移至 YomkLogApi.h——发射域 yomk::log()（主控制台 / .to_sink(name) 发射
    // 终端 ×6，位置捕获经域入口默认实参）+ 管理域 yomk::log_mgr()（门控 / 代理 / 位置前缀 /
    // 生命周期 / 内省）；emit 公共垫片 ×12、mainLoggerTag、管理面成员与 24 个日志宏随迁移删除，
    // 调用点全量切链式
    // CONTEXT_API：已整体迁移至 YomkContextApi.h 的 yomk::ctx() 域（key/group 累积 +
    // 写/读终结 + msg<T>() 类型化分面），14 个成员已删、调用点全量切链式
    // EVENTLOOP_API：已整体迁移至 YomkEventLoopApi.h 的 yomk::event_loop() 域（loop(name) 分面 +
    // 参数前置累积、终结 op 无参；postAndWait 更名 post_wait、info 更名 get_info，
    // getEventLoopInfo 双重载合并为 tag_count 累积（省略 = 缺省 3）），9 个成员已删、调用点全量切链式
    // FUNCTIONPOOL_API：已整体迁移至 YomkFunctionPoolApi.h 的 yomk::fn_pool() 域（fn_name(name)
    // 承载函数名 + 参数前置累积、终结 op 无参；register/unregister 更名 install/uninstall——
    // register 为 C++ 保留字，getFunctionInfo 更名 get_info 对齐 log/event_loop 域），
    // 6 个成员已删、调用点全量切链式
    // SERVER_INFO_API
public:
    // 查询所有服务名称
    static yomk::payload::Response listServices()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvServices).request();
    }
    // 查询指定服务的全部功能函数（返回 FuncInfoList 结构化清单：m_funcName/m_msgName/m_description，
    // 与 /function 单查、/all 聚合、函数池内省同族；m_msg 恒 "ok"，不存在服务为空清单）
    static yomk::payload::Response listServiceFunctions(const std::string& srvName)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunctions).msg(yomk::msgs::String::shared(srvName)).request();
    }
    // 查询指定功能函数元信息
    static yomk::payload::Response getServiceFunctionInfoByUrl(const std::string& url)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvFunction).msg(yomk::msgs::String::shared(url)).request();
    }
    // 查询框架全量服务及功能函数拓扑
    static yomk::payload::Response getAllServerInfo()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::srvAll).request();
    }

};
