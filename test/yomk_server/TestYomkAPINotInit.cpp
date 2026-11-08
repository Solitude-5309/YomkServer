// 链式 API 未初始化守卫全集测试
//
// 覆盖内容：
// 1. version() 免初始化可调用（不依赖单例，返回非空版本号）
// 2. 未初始化守卫默认值：instance()() 为空、shutdown() 安全空转（幂等）；
// request 返回 ResStatus::Invalid + 契约消息 "YomkServer is not init"；asyncRequest 静默返回
// 回调不触发；create()/add()/remove() 返回 -1
// 3. 模块 API 守卫：五模块全量——
// EventLoop 9 入口、FunctionPool 6 入口、Logger 23 入口、Context 12 入口 + GET 特殊形态、
// ServerInfo 4 入口；yomk::payload::Response 形态 → ResStatus::Invalid；
// 类型化读 get() 形态 → 原样返回调用方累积的默认值（同一 requireServer 守卫）
//
// 说明：call_once 不可逆，未初始化态须独占进程（本程序全程不调 init）；add() 传裸指针
// 时守卫先于所有权接管返回，注册失败指针仍归调用方（测试侧 delete 验证不泄漏不崩溃）
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "YomkServerApi.h"
#include "YomkContextApi.h"
#include "YomkEventLoopApi.h"
#include "YomkFunctionPoolApi.h"
#include "YomkLogApi.h"

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

// 最小演示服务：未初始化守卫用例的模板实参载体（守卫先于构造，init 不会被调用）
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
        std::string ver = yomk::server().version();
        YOMK_TEST_CHECK(!ver.empty(), "version() 未初始化时返回非空版本号: " + ver);
    }

    std::cout << "===== 2. instance() 与未初始化 shutdown 空转 =====" << std::endl;
    {
        YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "instance()() 未初始化返回 nullptr");

        // 未初始化 shutdown 应安全空转（快照为空直接返回），不崩溃、不产生副作用
        yomk::server().shutdown();
        YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "shutdown() 未初始化空转，单例保持为空");
    }

    std::cout << "===== 3. request/asyncRequest 未初始化守卫 =====" << std::endl;
    {
        yomk::payload::Response resp = yomk::server().url("/Any/func").request();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Invalid, "request 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(resp.m_msg == "YomkServer is not init", "request 未初始化契约消息: " + resp.m_msg);

        yomk::server().url("/Any/func").async([](yomk::payload::Response) { ++g_asyncCbCount; }).request();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        YOMK_TEST_CHECK(g_asyncCbCount.load() == 0, "asyncRequest 未初始化静默返回，回调不触发");
    }

    std::cout << "===== 4. 服务管理 API 未初始化守卫 =====" << std::endl;
    {
        YOMK_TEST_CHECK(yomk::server().srv<DummySrv>().create() == -1, "create() 未初始化返回 -1");
        YOMK_TEST_CHECK(yomk::server().srv_ptr(nullptr).add() == -1, "add(nullptr) 未初始化返回 -1");

        // 守卫先于所有权接管返回，指针仍归调用方：delete 验证不双重释放
        DummySrv* heapSrv = new DummySrv(nullptr);
        YOMK_TEST_CHECK(yomk::server().srv_ptr(heapSrv).add() == -1, "add(裸指针) 未初始化返回 -1");
        delete heapSrv;
        YOMK_TEST_CHECK(true, "守卫拒绝后调用方 delete 无泄漏无崩溃");

        YOMK_TEST_CHECK(yomk::server().srv_name("/Any").remove() == -1, "remove() 未初始化返回 -1");
    }

    std::cout << "===== 5. 模块 API 守卫（五模块全量 + 类型化读 get() 特殊形态） =====" << std::endl;
    {
        // 五模块入口级全量（Logger 23 / EventLoop 9 / FunctionPool 6 /
        // Context 12 + GET 特殊形态 / ServerInfo 4）
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().proxy(nullptr).apply() == yomk::payload::ResStatus::Invalid,
            "console().proxy().apply() 未初始化返回 ResStatus::Invalid");
        auto logResp = yomk::log().tag("tag").info("hello");
        YOMK_TEST_CHECK(logResp == yomk::payload::ResStatus::Invalid, "log().info 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().tag("tag").warn("hello") == yomk::payload::ResStatus::Invalid,
            "log().warn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().tag("tag").error("hello") == yomk::payload::ResStatus::Invalid,
            "log().error 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().tag("tag").debug("hello") == yomk::payload::ResStatus::Invalid,
            "log().debug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().dir("/tmp").file("guard.log").create() == yomk::payload::ResStatus::Invalid,
            "dir().file().create() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().file("guard.log").flush() == yomk::payload::ResStatus::Invalid,
            "file().flush() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().to_sink("guard.log").tag("tag").info("hello") == yomk::payload::ResStatus::Invalid,
            "log().to_sink().info 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().to_sink("guard.log").tag("tag").warn("hello") == yomk::payload::ResStatus::Invalid,
            "log().to_sink().warn 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().to_sink("guard.log").tag("tag").error("hello") == yomk::payload::ResStatus::Invalid,
            "log().to_sink().error 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log().to_sink("guard.log").tag("tag").debug("hello") == yomk::payload::ResStatus::Invalid,
            "log().to_sink().debug 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).disable() == yomk::payload::ResStatus::Invalid,
            "console().level(Info).disable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Warn).disable() == yomk::payload::ResStatus::Invalid,
            "console().level(Warn).disable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Error).disable() == yomk::payload::ResStatus::Invalid,
            "console().level(Error).disable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).disable() == yomk::payload::ResStatus::Invalid,
            "console().level(Debug).disable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Info).enable() == yomk::payload::ResStatus::Invalid,
            "console().level(Info).enable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Warn).enable() == yomk::payload::ResStatus::Invalid,
            "console().level(Warn).enable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Error).enable() == yomk::payload::ResStatus::Invalid,
            "console().level(Error).enable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().level(yomk::payload::LogLevel::Debug).enable() == yomk::payload::ResStatus::Invalid,
            "console().level(Debug).enable() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console().list().m_status == yomk::payload::ResStatus::Invalid,
            "console().list() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().console("guard_logger").get_info().m_status == yomk::payload::ResStatus::Invalid,
            "console().get_info() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(yomk::log_mgr().all().m_status == yomk::payload::ResStatus::Invalid, "all() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::log_mgr().file("guard_logger").remove() == yomk::payload::ResStatus::Invalid,
            "file().remove() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").create() == yomk::payload::ResStatus::Invalid,
            "createContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").set() == yomk::payload::ResStatus::Invalid,
            "setContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").destroy() == yomk::payload::ResStatus::Invalid,
            "destroyContext 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().checker().enable() == yomk::payload::ResStatus::Invalid,
            "enableContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().checker().disable() == yomk::payload::ResStatus::Invalid,
            "disableContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").checker_fn(nullptr).apply() == yomk::payload::ResStatus::Invalid,
            "setContextChecker 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().monitor().enable() == yomk::payload::ResStatus::Invalid,
            "enableContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().monitor().disable() == yomk::payload::ResStatus::Invalid,
            "disableContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").monitor_fn(nullptr).apply() == yomk::payload::ResStatus::Invalid,
            "setContextMonitor 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().keys().m_status == yomk::payload::ResStatus::Invalid,
            "ctx().keys() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().key("guard_key").info().m_status == yomk::payload::ResStatus::Invalid,
            "ctx().key().info() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::ctx().all().m_status == yomk::payload::ResStatus::Invalid,
            "getAllContextInfo 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("/guard_loop").start() == yomk::payload::ResStatus::Invalid,
            "loop(name).start() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("/guard_loop").stop() == yomk::payload::ResStatus::Invalid,
            "loop(name).stop() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("x"))).loop("/guard_loop").post() ==
                yomk::payload::ResStatus::Invalid,
            "msg().loop(name).post() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().msg(yomk::msgs::String::shared(std::string("x"))).loop("/guard_loop").post_wait() ==
                yomk::payload::ResStatus::Invalid,
            "msg().loop(name).post_wait() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("/guard_loop").destroy() == yomk::payload::ResStatus::Invalid,
            "loop(name).destroy() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().list().m_status == yomk::payload::ResStatus::Invalid,
            "list() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("/guard_loop").get_info().m_status == yomk::payload::ResStatus::Invalid,
            "loop(name).get_info() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().loop("/guard_loop").tag_count(5).get_info().m_status == yomk::payload::ResStatus::Invalid,
            "loop(name).tag_count(n).get_info() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::event_loop().all().m_status == yomk::payload::ResStatus::Invalid,
            "all() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("/guard_func").fn(nullptr).install() == yomk::payload::ResStatus::Invalid,
            "fn_pool().install() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("/guard_func").uninstall() == yomk::payload::ResStatus::Invalid,
            "fn_pool().uninstall() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("/guard_func").msg(yomk::msgs::String::shared(std::string("x"))).call().m_status ==
                yomk::payload::ResStatus::Invalid,
            "fn_pool().call() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().list().m_status == yomk::payload::ResStatus::Invalid,
            "fn_pool().list() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("/guard_func").get_info().m_status == yomk::payload::ResStatus::Invalid,
            "fn_pool().get_info() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().all().m_status == yomk::payload::ResStatus::Invalid,
            "fn_pool().all() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(yomk::server().all().m_status == yomk::payload::ResStatus::Invalid, "server().all() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::server().services().m_status == yomk::payload::ResStatus::Invalid,
            "server().services() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::server().srv_name("/guard_srv").functions().m_status == yomk::payload::ResStatus::Invalid,
            "server().functions() 未初始化返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::server().fn_url("/guard_srv/f").get_info().m_status == yomk::payload::ResStatus::Invalid,
            "server().get_info() 未初始化返回 ResStatus::Invalid");

        // 类型化读 get() 特殊形态：守卫返回值即调用方默认值（指针一致）
        std::shared_ptr<yomk::msgs::String> ctxDefault = std::make_shared<yomk::msgs::String>("guard_default");
        std::shared_ptr<yomk::msgs::String> ctxGot = yomk::ctx().key("guard_key").msg<yomk::msgs::String>().with_default(ctxDefault).get();
        YOMK_TEST_CHECK(ctxGot == ctxDefault, "get() 未初始化原样返回调用方默认值");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
