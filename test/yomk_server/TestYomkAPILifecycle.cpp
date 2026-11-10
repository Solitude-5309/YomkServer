// server() init/shutdown 单例生命周期测试
//
// 覆盖内容：
// 1. call_once 三承诺：二次 init(n) 返回同一实例（asyncThreadCount 仅首次生效）；
// shutdown 后守卫回归未初始化态；关闭后 init 再调返回 nullptr（不支持二次初始化）
// 2. 内置服务：init 拉起 /YomkFunctionPool /YomkContext /YomkEventLoop /YomkLogger /YomkServerInfo
// 3. API 层独有守卫（已初始化态，区别于服务器层守卫）：add(nullptr) 返回 -1、
// remove("") 返回 -1；增删往返、重复删除与守卫放行至服务器层
// 4. 幂等：二次 shutdown 不崩溃；进程退出 atexit 幂等清理（正常退出码 0 即证）
// 5. 各域模块版本号：与框架版本一致（集中取项目版本号），shutdown 后仍可调用
//
// 说明：本程序验证已初始化态与关闭后态，与 TestYomkAPINotInit（未初始化态）进程隔离
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "yomk/server/YomkServerApi.h"
#include "yomk/srv_info/YomkServerInfoApi.h"
#include "yomk/ctx/YomkContextApi.h"
#include "yomk/event_loop/YomkEventLoopApi.h"
#include "yomk/fn_pool/YomkFunctionPoolApi.h"
#include "yomk/log/YomkLoggerApi.h"

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

static bool nameIn(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

// 最小演示服务：API 层增删往返与二次初始化守卫用例载体
class DummySrv : public YomkService
{
public:
    explicit DummySrv(YomkServer* server = nullptr) : YomkService(server) {}
    int init() override { return 0; }
};

int main()
{
    std::cout << "===== 1. init/call_once 单例承诺 =====" << std::endl;
    {
        yomk::server().async_thread(1).init();
        std::shared_ptr<YomkServer> first = yomk::server().instance();
        YOMK_TEST_CHECK(first != nullptr, "首次 init(1) 返回非空单例");

        yomk::server().async_thread(4).init();
        std::shared_ptr<YomkServer> second = yomk::server().instance();
        YOMK_TEST_CHECK(
            second != nullptr && second.get() == first.get(),
            "二次 init(4) 返回同一实例（asyncThreadCount 仅首次生效）");

        std::shared_ptr<YomkServer> inst = yomk::server().instance();
        YOMK_TEST_CHECK(inst.get() == first.get(), "instance()() 与 init 返回一致");
    }

    std::cout << "===== 2. 内置服务拉起 =====" << std::endl;
    {
        std::vector<std::string> names = yomk::server().instance()->serviceNames();
        YOMK_TEST_CHECK(nameIn(names, "/YomkFunctionPool"), "内置服务 /YomkFunctionPool 已拉起");
        YOMK_TEST_CHECK(nameIn(names, "/YomkContext"), "内置服务 /YomkContext 已拉起");
        YOMK_TEST_CHECK(nameIn(names, "/YomkEventLoop"), "内置服务 /YomkEventLoop 已拉起");
        YOMK_TEST_CHECK(nameIn(names, "/YomkLogger"), "内置服务 /YomkLogger 已拉起");
        YOMK_TEST_CHECK(nameIn(names, "/YomkServerInfo"), "内置服务 /YomkServerInfo 已拉起");
    }

    std::cout << "===== 3. API 层独有守卫（已初始化态） =====" << std::endl;
    {
        YOMK_TEST_CHECK(yomk::server().srv_ptr(nullptr).add() == -1, "add(nullptr) 返回 -1（YomkService is null）");
        YOMK_TEST_CHECK(yomk::server().srv_name("").remove() == -1, "remove(\"\") 返回 -1（service name is empty）");
    }

    std::cout << "===== 4. API 层增删往返 =====" << std::endl;
    {
        YOMK_TEST_CHECK(
            yomk::server().srv_name("/ApiSrv").srv_ptr(new DummySrv(yomk::server().instance().get())).add() == 0,
            "add() 两参形式改名注册成功");
        YOMK_TEST_CHECK(yomk::server().srv_name("/ApiSrv").remove() == 0, "remove() 已注册服务返回 0");
        YOMK_TEST_CHECK(yomk::server().srv_name("/ApiSrv").remove() == -1, "重复删除已删服务返回 -1");

        // 已初始化时守卫放行至服务器层：不存在的服务名按服务器层契约返回 ResStatus::Failed
        yomk::payload::Response resp = yomk::server().url("/Nope/x").request();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Failed, "request 未知服务经守卫放行至服务器层返回 ResStatus::Failed");
    }

    std::cout << "===== 5. shutdown 与关闭后守卫回归 =====" << std::endl;
    {
        yomk::server().shutdown();
        YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "shutdown 后单例置空");

        yomk::server().shutdown();
        YOMK_TEST_CHECK(true, "二次 shutdown 幂等不崩溃");

        yomk::payload::Response resp = yomk::server().url("/Any/func").request();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Invalid, "关闭后 request 守卫回归 ResStatus::Invalid");
        YOMK_TEST_CHECK(resp.m_msg == "YomkServer is not initialized", "关闭后契约消息回归: " + resp.m_msg);
        YOMK_TEST_CHECK(yomk::server().srv<DummySrv>().create() == -1, "关闭后 create() 守卫回归 -1");

        yomk::server().init();
        YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "关闭后 init 再调不再重建，实例保持为空（call_once 已消耗，不支持二次初始化）");
    }

    std::cout << "===== 6. 各域模块版本号（shutdown 后仍可调用，编译期常量直返） =====" << std::endl;
    {
        const std::string fw = yomk::server().version();
        YOMK_TEST_CHECK(!fw.empty(), "框架版本号非空");
        // 当前各域版本号集中取项目版本号（见顶层 CMakeLists），与框架版本一致；
        // 某域独立 bump 版本后仅需更新该域断言
        YOMK_TEST_CHECK(yomk::ctx().version() == fw, "ctx 域版本号与框架一致: " + yomk::ctx().version());
        YOMK_TEST_CHECK(yomk::event_loop().version() == fw, "event_loop 域版本号与框架一致");
        YOMK_TEST_CHECK(yomk::fn_pool().version() == fw, "fn_pool 域版本号与框架一致");
        YOMK_TEST_CHECK(yomk::log_mgr().version() == fw, "log 域版本号与框架一致");
        YOMK_TEST_CHECK(yomk::srv_info().version() == fw, "srv_info 域版本号与框架一致");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
