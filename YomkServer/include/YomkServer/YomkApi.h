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
    // EVENTLOOP_API
public:
    // 启动指定事件循环
    static yomk::payload::ResStatus startEventLoop(
        const std::string& eventLoopName,
        YomkServiceFunc m_defaultServiceFunc = nullptr,
        const std::string& msgName = "")
    {
        return yomk::server().url(yomk::url::loopStart).msg(yomk::msgs::EventLoop::shared(yomk::payload::EventLoop{eventLoopName, m_defaultServiceFunc, msgName})).status();
    }
    // 停止事件循环线程（保留未执行事件）
    static yomk::payload::ResStatus stopEventLoop(const std::string& eventLoopName)
    {
        return yomk::server().url(yomk::url::loopStop).msg(yomk::msgs::String::shared(eventLoopName)).status();
    }
    // 异步投递事件
    static yomk::payload::ResStatus postEvent(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        return yomk::server().url(yomk::url::loopPost).msg(yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag))).status();
    }
    // 同步投递事件（阻塞等待执行完成）
    static yomk::payload::ResStatus postEventAndWait(
        const std::string& eventLoopName,
        YomkMsgPtr eventData,
        YomkServiceFunc eventHandle = nullptr,
        const std::string& tag = "")
    {
        return yomk::server().url(yomk::url::loopPostWait).msg(yomk::msgs::Event::shared(yomk::payload::Event(eventLoopName, eventData, eventHandle, tag))).status();
    }
    // 销毁事件循环并清空队列
    static yomk::payload::ResStatus destroyEventLoop(const std::string& eventLoopName)
    {
        return yomk::server().url(yomk::url::loopDestroy).msg(yomk::msgs::String::shared(eventLoopName)).status();
    }
    // 获取全部事件循环名称列表
    static yomk::payload::Response listEventLoops()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoops).request();
    }
    // 查询指定事件循环状态与事件积压数
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoop).msg(yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{eventLoopName})).request();
    }
    static yomk::payload::Response getEventLoopInfo(const std::string& eventLoopName, size_t tagCount)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopLoop).msg(yomk::msgs::EventLoopQuery::shared(yomk::payload::EventLoopQuery{eventLoopName, tagCount})).request();
    }
    // 查询全部事件循环状态
    static yomk::payload::Response getAllEventLoopInfo()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::loopAll).request();
    }
    // FUNCTIONPOOL_API
public:
    // 注册函数到全局函数池（msgName 声明期望消息类型；description 函数说明，内容使用者自定义，均可为空）
    static yomk::payload::ResStatus registerFunction(
        const std::string& funcName, YomkServiceFunc func, const std::string& msgName = "",
        const std::string& description = "")
    {
        return yomk::server().url(yomk::url::fpRegister).msg(yomk::msgs::Function::shared(yomk::payload::Function{funcName, func, msgName, description})).status();
    }
    // 从函数池注销函数
    static yomk::payload::ResStatus unregisterFunction(const std::string& funcName)
    {
        return yomk::server().url(yomk::url::fpUnregister).msg(yomk::msgs::String::shared(funcName)).status();
    }
    // 调用函数池中注册的函数
    static yomk::payload::Response callFunction(const std::string& funcName, YomkMsgPtr callData)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpCall).msg(yomk::msgs::CallFunction::shared(yomk::payload::CallFunction{funcName, callData})).request();
    }
    // 获取函数池中全部函数名
    static yomk::payload::Response listFunctionNames()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpNames).request();
    }
    // 查询单个已注册函数元信息
    static yomk::payload::Response getFunctionInfo(const std::string& funcName)
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpName).msg(yomk::msgs::String::shared(funcName)).request();
    }
    // 查询全部已注册函数元信息
    static yomk::payload::Response getAllFunctionInfo()
    {
        if (!yomk::detail::requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not init");
        }
        return yomk::server().url(yomk::url::fpAll).request();
    }
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
