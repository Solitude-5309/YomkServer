/**
 * @file TestYomkAPINotInit.cpp
 * @brief YomkAPI 未初始化守卫全集测试（正式全面测试，MC6）
 *
 * 覆盖内容：
 * 1. version() 免初始化可调用（不依赖单例，返回非空版本号）
 * 2. 未初始化守卫默认值：serverInstance() 为空、shutdown() 安全空转（幂等）；
 *    request 返回 ResStatus::Invalid + 契约消息 "YomkServer is not init"；asyncRequest 静默返回
 *    回调不触发；newService/addService/delService 返回 -1
 * 3. 模块 API 守卫：五模块全量（P1 补齐 Context/ServerInfo 共 14 条，原代表条目保留）——
 *    EventLoop 9 入口、FunctionPool 6 入口、Logger 23 入口（LG2 补齐原 19 项计数，LG4 新增
 *    deleteFileLogger（LG6 由 LOGGER_DELETE 改名））、Context 12 入口 + GET 特殊形态、
 *    ServerInfo 4 入口；YomkResponse 形态 → ResStatus::Invalid；
 *    getContext 形态 → 原样返回调用方默认值，同一守卫宏展开书面豁免
 *
 * 说明：call_once 不可逆，未初始化态须独占进程（本程序全程不调 init）；addService 传裸指针
 *       时守卫先于所有权接管返回，注册失败指针仍归调用方（测试侧 delete 验证不泄漏不崩溃）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "YomkAPI.h"

static int g_failed = 0;

#define YOMK_TEST_CHECK(cond, msg)                                                          \
    do                                                                            \
    {                                                                             \
        if (!(cond))                                                              \
        {                                                                         \
            std::cout << "[FAIL] [line " << __LINE__ << "] " << msg << std::endl; \
            ++g_failed;                                                           \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            std::cout << "[ OK ] [line " << __LINE__ << "] " << msg << std::endl; \
        }                                                                         \
    } while (0)

static std::atomic<int> g_asyncCbCount{0};  // 未初始化 asyncRequest 回调到达计数（应恒为 0）

/**
 * @brief 最小演示服务：未初始化守卫用例的模板实参载体（守卫先于构造，init 不会被调用）
 */
class DummySrv : public YomkService
{
public:
    explicit DummySrv(YomkServer* server = nullptr) : YomkService(server) {}
    int init() override { return 0; }
};

int main()
{
    std::cout << "===== 1. version 免初始化可调用 =====" << std::endl;
    {
        // YOMKSERVER_VERSION 为库私有编译定义（target_compile_definitions PRIVATE），
        // 测试侧不引用宏，以返回非空字符串实证免初始化可用
        std::string ver = YomkAPI::version();
        YOMK_TEST_CHECK(!ver.empty(), "version() 未初始化时返回非空版本号: " + ver);
    }

    std::cout << "===== 2. serverInstance 与未初始化 shutdown 空转 =====" << std::endl;
    {
        YOMK_TEST_CHECK(YomkAPI::serverInstance() == nullptr, "serverInstance() 未初始化返回 nullptr");

        // 未初始化 shutdown 应安全空转（快照为空直接返回），不崩溃、不产生副作用
        YomkAPI::shutdown();
        YOMK_TEST_CHECK(YomkAPI::serverInstance() == nullptr, "shutdown() 未初始化空转，单例保持为空");
    }

    std::cout << "===== 3. request/asyncRequest 未初始化守卫 =====" << std::endl;
    {
        YomkResponse resp = YomkAPI::request("/Any/func", nullptr);
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::ResStatus::Invalid, "request 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(resp.m_msg == "YomkServer is not init", "request 未初始化契约消息: " + resp.m_msg);

        YomkAPI::asyncRequest("/Any/func", nullptr, [](YomkResponse) { ++g_asyncCbCount; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        YOMK_TEST_CHECK(g_asyncCbCount.load() == 0, "asyncRequest 未初始化静默返回，回调不触发");
    }

    std::cout << "===== 4. 服务管理 API 未初始化守卫 =====" << std::endl;
    {
        YOMK_TEST_CHECK(YomkAPI::newService<DummySrv>() == -1, "newService 未初始化返回 -1");
        YOMK_TEST_CHECK(YomkAPI::addService(nullptr) == -1, "addService(nullptr) 未初始化返回 -1");

        // 守卫先于所有权接管返回，指针仍归调用方：delete 验证不双重释放
        DummySrv* heapSrv = new DummySrv(nullptr);
        YOMK_TEST_CHECK(YomkAPI::addService(heapSrv) == -1, "addService(裸指针) 未初始化返回 -1");
        delete heapSrv;
        YOMK_TEST_CHECK(true, "守卫拒绝后调用方 delete 无泄漏无崩溃");

        YOMK_TEST_CHECK(YomkAPI::delService("/Any") == -1, "delService 未初始化返回 -1");
    }

    std::cout << "===== 5. 模块 API 守卫（五模块全量 + getContext 特殊形态） =====" << std::endl;
    {
        // 五模块入口级全量（P1 收口：Logger 23 / EventLoop 9 / FunctionPool 6 /
        // Context 12 + GET 特殊形态 / ServerInfo 4）
        YOMK_TEST_CHECK(
            YomkAPI::setConsoleLogProxy(nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "setConsoleLogProxy 未初始化返回 ResStatus::Invalid");
        YomkResponse logResp = YomkAPI::emitConsoleLogInfo("tag", "hello");
        YOMK_TEST_CHECK(logResp.m_status == YomkResponse::ResStatus::Invalid, "emitConsoleLogInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(logResp.m_msg == "YomkServer is not init", "模块 API 守卫契约消息同根: " + logResp.m_msg);
        YOMK_TEST_CHECK(
            YomkAPI::emitConsoleLogWarn("tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitConsoleLogWarn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitConsoleLogError("tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitConsoleLogError 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitConsoleLogDebug("tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitConsoleLogDebug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::createFileLogger("/tmp", "guard.log").m_status == YomkResponse::ResStatus::Invalid,
            "createFileLogger 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::flushFileLog("guard.log").m_status == YomkResponse::ResStatus::Invalid,
            "flushFileLog 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitFileLogInfo("guard.log", "tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitFileLogInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitFileLogWarn("guard.log", "tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitFileLogWarn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitFileLogError("guard.log", "tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitFileLogError 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::emitFileLogDebug("guard.log", "tag", "hello").m_status == YomkResponse::ResStatus::Invalid,
            "emitFileLogDebug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableConsoleLogInfo().m_status == YomkResponse::ResStatus::Invalid,
            "disableConsoleLogInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableConsoleLogWarn().m_status == YomkResponse::ResStatus::Invalid,
            "disableConsoleLogWarn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableConsoleLogError().m_status == YomkResponse::ResStatus::Invalid,
            "disableConsoleLogError 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableConsoleLogDebug().m_status == YomkResponse::ResStatus::Invalid,
            "disableConsoleLogDebug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableConsoleLogInfo().m_status == YomkResponse::ResStatus::Invalid,
            "enableConsoleLogInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableConsoleLogWarn().m_status == YomkResponse::ResStatus::Invalid,
            "enableConsoleLogWarn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableConsoleLogError().m_status == YomkResponse::ResStatus::Invalid,
            "enableConsoleLogError 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableConsoleLogDebug().m_status == YomkResponse::ResStatus::Invalid,
            "enableConsoleLogDebug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listLoggers("console").m_status == YomkResponse::ResStatus::Invalid,
            "listLoggers 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getLoggerInfo("guard_logger", "console").m_status == YomkResponse::ResStatus::Invalid,
            "getLoggerInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(YomkAPI::getAllLoggerInfo().m_status == YomkResponse::ResStatus::Invalid, "getAllLoggerInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::deleteFileLogger("guard_logger").m_status == YomkResponse::ResStatus::Invalid,
            "deleteFileLogger 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::createContext("guard_key", nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "createContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::setContext("guard_key", nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "setContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::destroyContext("guard_key").m_status == YomkResponse::ResStatus::Invalid,
            "destroyContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableContextChecker().m_status == YomkResponse::ResStatus::Invalid,
            "enableContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableContextChecker().m_status == YomkResponse::ResStatus::Invalid,
            "disableContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::setContextChecker("guard_key", nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "setContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::enableContextMonitor().m_status == YomkResponse::ResStatus::Invalid,
            "enableContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::disableContextMonitor().m_status == YomkResponse::ResStatus::Invalid,
            "disableContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::setContextMonitor("guard_key", nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "setContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listContextKeys().m_status == YomkResponse::ResStatus::Invalid,
            "listContextKeys 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getContextInfo("guard_key").m_status == YomkResponse::ResStatus::Invalid,
            "getContextInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getAllContextInfo().m_status == YomkResponse::ResStatus::Invalid,
            "getAllContextInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::startEventLoop("/guard_loop").m_status == YomkResponse::ResStatus::Invalid,
            "startEventLoop 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::stopEventLoop("/guard_loop").m_status == YomkResponse::ResStatus::Invalid,
            "stopEventLoop 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::postEvent("/guard_loop", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::ResStatus::Invalid,
            "YomkAPI::postEvent 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::postEventAndWait("/guard_loop", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::ResStatus::Invalid,
            "YomkAPI::postEventAndWait 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::destroyEventLoop("/guard_loop").m_status == YomkResponse::ResStatus::Invalid,
            "destroyEventLoop 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listEventLoops().m_status == YomkResponse::ResStatus::Invalid,
            "listEventLoops 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getEventLoopInfo("/guard_loop").m_status == YomkResponse::ResStatus::Invalid,
            "getEventLoopInfo(name) 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getEventLoopInfo("/guard_loop", 5).m_status == YomkResponse::ResStatus::Invalid,
            "getEventLoopInfo(name, n) 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getAllEventLoopInfo().m_status == YomkResponse::ResStatus::Invalid,
            "getAllEventLoopInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::registerFunction("/guard_func", nullptr).m_status == YomkResponse::ResStatus::Invalid,
            "registerFunction 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::unregisterFunction("/guard_func").m_status == YomkResponse::ResStatus::Invalid,
            "unregisterFunction 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::callFunction("/guard_func", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::ResStatus::Invalid,
            "callFunction 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listFunctionNames().m_status == YomkResponse::ResStatus::Invalid,
            "listFunctionNames 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getFunctionInfo("/guard_func").m_status == YomkResponse::ResStatus::Invalid,
            "getFunctionInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getAllFunctionInfo().m_status == YomkResponse::ResStatus::Invalid,
            "getAllFunctionInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(YomkAPI::getAllServerInfo().m_status == YomkResponse::ResStatus::Invalid, "getAllServerInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listServices().m_status == YomkResponse::ResStatus::Invalid,
            "listServices 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::listServiceFunctions("/guard_srv").m_status == YomkResponse::ResStatus::Invalid,
            "listServiceFunctions 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            YomkAPI::getServiceFunctionInfoByUrl("/guard_srv/f").m_status == YomkResponse::ResStatus::Invalid,
            "getServiceFunctionInfoByUrl 未初始化返回 ResStatus::Invalid");

        // getContext 特殊形态：守卫返回值即调用方默认值（指针一致）
        std::shared_ptr<yomkmsg::String> ctxDefault = std::make_shared<yomkmsg::String>("guard_default");
        std::shared_ptr<yomkmsg::String> ctxGot = YomkAPI::getContext<yomkmsg::String>("guard_key", ctxDefault);
        YOMK_TEST_CHECK(ctxGot == ctxDefault, "getContext 未初始化原样返回调用方默认值");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
