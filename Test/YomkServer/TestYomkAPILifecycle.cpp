/**
 * @file TestYomkAPILifecycle.cpp
 * @brief YomkAPI init/shutdown 单例生命周期测试（正式全面测试，MC6）
 *
 * 覆盖内容：
 * 1. call_once 三承诺：二次 init(n) 返回同一实例（asyncThreadCount 仅首次生效）；
 *    shutdown 后守卫回归未初始化态；关闭后 init 再调返回 nullptr（不支持二次初始化）
 * 2. 内置服务：init 拉起 /YomkFunctionPool /YomkContext /YomkEventLoop /YomkLogger /YomkServerInfo
 * 3. API 层独有守卫（已初始化态，区别于服务器层守卫）：addService(nullptr) 返回 -1、
 *    delService("") 返回 -1；增删往返、重复删除与守卫放行至服务器层
 * 4. 幂等：二次 shutdown 不崩溃；进程退出 atexit 幂等清理（正常退出码 0 即证）
 *
 * 说明：本程序验证已初始化态与关闭后态，与 TestYomkAPINotInit（未初始化态）进程隔离
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "YomkAPI.h"

static int g_failed = 0;

#define CHECK(cond, msg)                                                          \
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

static bool nameIn(const std::vector<std::string> &names, const std::string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

/**
 * @brief 最小演示服务：API 层增删往返与二次初始化守卫用例载体
 */
class DummySrv : public YomkService
{
public:
    explicit DummySrv(YomkServer *server = nullptr)
        : YomkService(server) {}
    int init() override { return 0; }
};

int main()
{
    std::cout << "===== 1. init/call_once 单例承诺 =====" << std::endl;
    {
        std::shared_ptr<YomkServer> first = YomkAPI::init(1);
        CHECK(first != nullptr, "首次 init(1) 返回非空单例");

        std::shared_ptr<YomkServer> second = YomkAPI::init(4);
        CHECK(second != nullptr && second.get() == first.get(),
              "二次 init(4) 返回同一实例（asyncThreadCount 仅首次生效）");

        std::shared_ptr<YomkServer> inst = YomkAPI::serverInstance();
        CHECK(inst.get() == first.get(), "serverInstance() 与 init 返回一致");
    }

    std::cout << "===== 2. 内置服务拉起 =====" << std::endl;
    {
        std::vector<std::string> names = YomkAPI::serverInstance()->serviceNames();
        CHECK(nameIn(names, "/YomkFunctionPool"), "内置服务 /YomkFunctionPool 已拉起");
        CHECK(nameIn(names, "/YomkContext"), "内置服务 /YomkContext 已拉起");
        CHECK(nameIn(names, "/YomkEventLoop"), "内置服务 /YomkEventLoop 已拉起");
        CHECK(nameIn(names, "/YomkLogger"), "内置服务 /YomkLogger 已拉起");
        CHECK(nameIn(names, "/YomkServerInfo"), "内置服务 /YomkServerInfo 已拉起");
    }

    std::cout << "===== 3. API 层独有守卫（已初始化态） =====" << std::endl;
    {
        CHECK(YomkAPI::addService(nullptr) == -1, "addService(nullptr) 返回 -1（YomkService is null）");
        CHECK(YomkAPI::delService("") == -1, "delService(\"\") 返回 -1（service name is empty）");
    }

    std::cout << "===== 4. API 层增删往返 =====" << std::endl;
    {
        CHECK(YomkAPI::addService(new DummySrv(YomkAPI::serverInstance().get()), "/ApiSrv") == 0,
              "addService 两参形式改名注册成功");
        CHECK(YomkAPI::delService("/ApiSrv") == 0, "delService 已注册服务返回 0");
        CHECK(YomkAPI::delService("/ApiSrv") == -1, "重复删除已删服务返回 -1");

        // 已初始化时守卫放行至服务器层：不存在的服务名按服务器层契约返回 eNo
        YomkResponse resp = YomkAPI::request("/Nope/x", nullptr);
        CHECK(resp.m_status == YomkResponse::eNo, "request 未知服务经守卫放行至服务器层返回 eNo");
    }

    std::cout << "===== 5. shutdown 与关闭后守卫回归 =====" << std::endl;
    {
        YomkAPI::shutdown();
        CHECK(YomkAPI::serverInstance() == nullptr, "shutdown 后单例置空");

        YomkAPI::shutdown();
        CHECK(true, "二次 shutdown 幂等不崩溃");

        YomkResponse resp = YomkAPI::request("/Any/func", nullptr);
        CHECK(resp.m_status == YomkResponse::eInvalid, "关闭后 request 守卫回归 eInvalid");
        CHECK(resp.m_msg == "YomkServer is not init", "关闭后契约消息回归: " + resp.m_msg);
        CHECK(YomkAPI::newService<DummySrv>() == -1, "关闭后 newService 守卫回归 -1");

        CHECK(YomkAPI::init() == nullptr, "关闭后 init 再调返回 nullptr（call_once 已消耗，不支持二次初始化）");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
