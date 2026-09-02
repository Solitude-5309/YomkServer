/**
 * @file TestYomkAPINotInit.cpp
 * @brief YomkAPI 未初始化守卫全集测试（正式全面测试，MC6）
 *
 * 覆盖内容：
 * 1. version() 免初始化可调用（不依赖单例，返回非空版本号）
 * 2. 未初始化守卫默认值：serverInstance() 为空、shutdown() 安全空转（幂等）；
 *    request 返回 eInvalid + 契约消息 "YomkServer is not init"；asyncRequest 静默返回
 *    回调不触发；newService/addService/delService 返回 -1
 * 3. 模块 API 守卫代表：每种返回值形态取代表（YomkResponse 形态 → eInvalid；
 *    CONTEXT_GET 形态 → 原样返回调用方默认值），其余模块 API 为同一守卫宏展开，书面豁免
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

static std::atomic<int> g_asyncCbCount{0}; // 未初始化 asyncRequest 回调到达计数（应恒为 0）

/**
 * @brief 最小演示服务：未初始化守卫用例的模板实参载体（守卫先于构造，init 不会被调用）
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
    std::cout << "===== 1. version 免初始化可调用 =====" << std::endl;
    {
        // YOMKSERVER_VERSION 为库私有编译定义（target_compile_definitions PRIVATE），
        // 测试侧不引用宏，以返回非空字符串实证免初始化可用
        std::string ver = YomkAPI::version();
        CHECK(!ver.empty(), "version() 未初始化时返回非空版本号: " + ver);
    }

    std::cout << "===== 2. serverInstance 与未初始化 shutdown 空转 =====" << std::endl;
    {
        CHECK(YomkAPI::serverInstance() == nullptr, "serverInstance() 未初始化返回 nullptr");

        // 未初始化 shutdown 应安全空转（快照为空直接返回），不崩溃、不产生副作用
        YomkAPI::shutdown();
        CHECK(YomkAPI::serverInstance() == nullptr, "shutdown() 未初始化空转，单例保持为空");
    }

    std::cout << "===== 3. request/asyncRequest 未初始化守卫 =====" << std::endl;
    {
        YomkResponse resp = YomkAPI::request("/Any/func", nullptr);
        CHECK(resp.m_status == YomkResponse::eInvalid, "request 未初始化返回 eInvalid");
        CHECK(resp.m_msg == "YomkServer is not init", "request 未初始化契约消息: " + resp.m_msg);

        YomkAPI::asyncRequest("/Any/func", nullptr, [](YomkResponse)
                              { ++g_asyncCbCount; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(g_asyncCbCount.load() == 0, "asyncRequest 未初始化静默返回，回调不触发");
    }

    std::cout << "===== 4. 服务管理 API 未初始化守卫 =====" << std::endl;
    {
        CHECK(YomkAPI::newService<DummySrv>() == -1, "newService 未初始化返回 -1");
        CHECK(YomkAPI::addService(nullptr) == -1, "addService(nullptr) 未初始化返回 -1");

        // 守卫先于所有权接管返回，指针仍归调用方：delete 验证不双重释放
        DummySrv *heapSrv = new DummySrv(nullptr);
        CHECK(YomkAPI::addService(heapSrv) == -1, "addService(裸指针) 未初始化返回 -1");
        delete heapSrv;
        CHECK(true, "守卫拒绝后调用方 delete 无泄漏无崩溃");

        CHECK(YomkAPI::delService("/Any") == -1, "delService 未初始化返回 -1");
    }

    std::cout << "===== 5. 模块 API 守卫代表（每种返回值形态一个代表，其余同宏展开书面豁免） =====" << std::endl;
    {
        // YomkResponse 形态代表（LOG/CONTEXT/EVENTLOOP/FUNCTIONPOOL/SERVER_INFO 各模块取代表）
        CHECK(YomkAPI::SET_CONSOLE_LOG_PROXY(nullptr).m_status == YomkResponse::eInvalid,
              "SET_CONSOLE_LOG_PROXY 未初始化返回 eInvalid");
        YomkResponse logResp = YomkAPI::CONSOLE_LOG_INFO_TAG("tag", "hello");
        CHECK(logResp.m_status == YomkResponse::eInvalid, "CONSOLE_LOG_INFO_TAG 未初始化返回 eInvalid");
        CHECK(logResp.m_msg == "YomkServer is not init", "模块 API 守卫契约消息同根: " + logResp.m_msg);
        CHECK(YomkAPI::FILE_LOG_CREATE("/tmp", "guard.log").m_status == YomkResponse::eInvalid,
              "FILE_LOG_CREATE 未初始化返回 eInvalid");
        CHECK(YomkAPI::ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eInvalid,
              "ON_CONSOLE_LOG_INFO 未初始化返回 eInvalid");
        CHECK(YomkAPI::LOGGER_INFO_ALL().m_status == YomkResponse::eInvalid,
              "LOGGER_INFO_ALL 未初始化返回 eInvalid");
        CHECK(YomkAPI::CONTEXT_CREATE("guard_key", nullptr).m_status == YomkResponse::eInvalid,
              "CONTEXT_CREATE 未初始化返回 eInvalid");
        CHECK(YomkAPI::EVENTLOOP_START("/guard_loop").m_status == YomkResponse::eInvalid,
              "EVENTLOOP_START 未初始化返回 eInvalid");
        CHECK(YomkAPI::FUNCTIONPOOL_REGISTER("/guard_func", nullptr).m_status == YomkResponse::eInvalid,
              "FUNCTIONPOOL_REGISTER 未初始化返回 eInvalid");
        CHECK(YomkAPI::SERVER_INFO_ALL().m_status == YomkResponse::eInvalid,
              "SERVER_INFO_ALL 未初始化返回 eInvalid");

        // CONTEXT_GET 特殊形态：守卫返回值即调用方默认值（指针一致）
        std::shared_ptr<Yomk(String)> ctxDefault = std::make_shared<Yomk(String)>("guard_default");
        std::shared_ptr<Yomk(String)> ctxGot = YomkAPI::CONTEXT_GET<Yomk(String)>("String", "guard_key", ctxDefault);
        CHECK(ctxGot == ctxDefault, "CONTEXT_GET 未初始化原样返回调用方默认值");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
