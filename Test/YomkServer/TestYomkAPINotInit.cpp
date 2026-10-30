/**
 * @file TestYomkAPINotInit.cpp
 * @brief YomkAPI 未初始化守卫全集测试（正式全面测试，MC6）
 *
 * 覆盖内容：
 * 1. version() 免初始化可调用（不依赖单例，返回非空版本号）
 * 2. 未初始化守卫默认值：serverInstance() 为空、shutdown() 安全空转（幂等）；
 *    request 返回 eInvalid + 契约消息 "YomkServer is not init"；asyncRequest 静默返回
 *    回调不触发；newService/addService/delService 返回 -1
 * 3. 模块 API 守卫：五模块全量（P1 补齐 Context/ServerInfo 共 14 条，原代表条目保留）——
 *    EventLoop 9 入口、FunctionPool 6 入口、Logger 23 入口（LG2 补齐原 19 项计数，LG4 新增
 *    FILE_LOG_DELETE（LG6 由 LOGGER_DELETE 改名））、Context 12 入口 + GET 特殊形态、
 *    ServerInfo 4 入口；YomkResponse 形态 → eInvalid；
 *    CONTEXT_GET 形态 → 原样返回调用方默认值，同一守卫宏展开书面豁免
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
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eInvalid, "request 未初始化返回 eInvalid");
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

    std::cout << "===== 5. 模块 API 守卫（五模块全量 + CONTEXT_GET 特殊形态） =====" << std::endl;
    {
        // 五模块入口级全量（P1 收口：Logger 23 / EventLoop 9 / FunctionPool 6 /
        // Context 12 + GET 特殊形态 / ServerInfo 4）
        YOMK_TEST_CHECK(
            YomkAPI::SET_CONSOLE_LOG_PROXY(nullptr).m_status == YomkResponse::eInvalid,
            "SET_CONSOLE_LOG_PROXY 未初始化返回 eInvalid");
        YomkResponse logResp = YomkAPI::CONSOLE_LOG_INFO_TAG("tag", "hello");
        YOMK_TEST_CHECK(logResp.m_status == YomkResponse::eInvalid, "CONSOLE_LOG_INFO_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(logResp.m_msg == "YomkServer is not init", "模块 API 守卫契约消息同根: " + logResp.m_msg);
        YOMK_TEST_CHECK(
            YomkAPI::CONSOLE_LOG_WARN_TAG("tag", "hello").m_status == YomkResponse::eInvalid,
            "CONSOLE_LOG_WARN_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONSOLE_LOG_ERROR_TAG("tag", "hello").m_status == YomkResponse::eInvalid,
            "CONSOLE_LOG_ERROR_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONSOLE_LOG_DEBUG_TAG("tag", "hello").m_status == YomkResponse::eInvalid,
            "CONSOLE_LOG_DEBUG_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_CREATE("/tmp", "guard.log").m_status == YomkResponse::eInvalid,
            "FILE_LOG_CREATE 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_WRITE("guard.log").m_status == YomkResponse::eInvalid,
            "FILE_LOG_WRITE 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_INFO_TAG("guard.log", "tag", "hello").m_status == YomkResponse::eInvalid,
            "FILE_LOG_INFO_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_WARN_TAG("guard.log", "tag", "hello").m_status == YomkResponse::eInvalid,
            "FILE_LOG_WARN_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_ERROR_TAG("guard.log", "tag", "hello").m_status == YomkResponse::eInvalid,
            "FILE_LOG_ERROR_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_DEBUG_TAG("guard.log", "tag", "hello").m_status == YomkResponse::eInvalid,
            "FILE_LOG_DEBUG_TAG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::OFF_CONSOLE_LOG_INFO().m_status == YomkResponse::eInvalid,
            "OFF_CONSOLE_LOG_INFO 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::OFF_CONSOLE_LOG_WARN().m_status == YomkResponse::eInvalid,
            "OFF_CONSOLE_LOG_WARN 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::OFF_CONSOLE_LOG_ERROR().m_status == YomkResponse::eInvalid,
            "OFF_CONSOLE_LOG_ERROR 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::OFF_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eInvalid,
            "OFF_CONSOLE_LOG_DEBUG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::ON_CONSOLE_LOG_INFO().m_status == YomkResponse::eInvalid,
            "ON_CONSOLE_LOG_INFO 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::ON_CONSOLE_LOG_WARN().m_status == YomkResponse::eInvalid,
            "ON_CONSOLE_LOG_WARN 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::ON_CONSOLE_LOG_ERROR().m_status == YomkResponse::eInvalid,
            "ON_CONSOLE_LOG_ERROR 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::ON_CONSOLE_LOG_DEBUG().m_status == YomkResponse::eInvalid,
            "ON_CONSOLE_LOG_DEBUG 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::LOGGER_INFO_LOGGERS("console").m_status == YomkResponse::eInvalid,
            "LOGGER_INFO_LOGGERS 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::LOGGER_INFO_LOGGER("guard_logger", "console").m_status == YomkResponse::eInvalid,
            "LOGGER_INFO_LOGGER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(YomkAPI::LOGGER_INFO_ALL().m_status == YomkResponse::eInvalid, "LOGGER_INFO_ALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FILE_LOG_DELETE("guard_logger").m_status == YomkResponse::eInvalid,
            "FILE_LOG_DELETE 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_CREATE("guard_key", nullptr).m_status == YomkResponse::eInvalid,
            "CONTEXT_CREATE 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_SET("guard_key", nullptr).m_status == YomkResponse::eInvalid,
            "CONTEXT_SET 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_DESTROY("guard_key").m_status == YomkResponse::eInvalid,
            "CONTEXT_DESTROY 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_ON_CHECKER().m_status == YomkResponse::eInvalid,
            "CONTEXT_ON_CHECKER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_OFF_CHECKER().m_status == YomkResponse::eInvalid,
            "CONTEXT_OFF_CHECKER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_SET_CHECKER("guard_key", nullptr).m_status == YomkResponse::eInvalid,
            "CONTEXT_SET_CHECKER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_ON_MONITOR().m_status == YomkResponse::eInvalid,
            "CONTEXT_ON_MONITOR 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_OFF_MONITOR().m_status == YomkResponse::eInvalid,
            "CONTEXT_OFF_MONITOR 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_SET_MONITOR("guard_key", nullptr).m_status == YomkResponse::eInvalid,
            "CONTEXT_SET_MONITOR 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_INFO_KEYS().m_status == YomkResponse::eInvalid,
            "CONTEXT_INFO_KEYS 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_INFO_KEY("guard_key").m_status == YomkResponse::eInvalid,
            "CONTEXT_INFO_KEY 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::CONTEXT_INFO_ALL().m_status == YomkResponse::eInvalid,
            "CONTEXT_INFO_ALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_START("/guard_loop").m_status == YomkResponse::eInvalid,
            "EVENTLOOP_START 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_STOP("/guard_loop").m_status == YomkResponse::eInvalid,
            "EVENTLOOP_STOP 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST("/guard_loop", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::eInvalid,
            "EVENTLOOP_POST 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_POST_WAIT("/guard_loop", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::eInvalid,
            "EVENTLOOP_POST_WAIT 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_DESTROY("/guard_loop").m_status == YomkResponse::eInvalid,
            "EVENTLOOP_DESTROY 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_INFO_LOOPS().m_status == YomkResponse::eInvalid,
            "EVENTLOOP_INFO_LOOPS 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_INFO_LOOP("/guard_loop").m_status == YomkResponse::eInvalid,
            "EVENTLOOP_INFO_LOOP(name) 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_INFO_LOOP("/guard_loop", 5).m_status == YomkResponse::eInvalid,
            "EVENTLOOP_INFO_LOOP(name, n) 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::EVENTLOOP_INFO_ALL().m_status == YomkResponse::eInvalid,
            "EVENTLOOP_INFO_ALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_REGISTER("/guard_func", nullptr).m_status == YomkResponse::eInvalid,
            "FUNCTIONPOOL_REGISTER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_UNREGISTER("/guard_func").m_status == YomkResponse::eInvalid,
            "FUNCTIONPOOL_UNREGISTER 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_CALL("/guard_func", yomkmsg::String::shared(std::string("x"))).m_status ==
                YomkResponse::eInvalid,
            "FUNCTIONPOOL_CALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_INFO_NAMES().m_status == YomkResponse::eInvalid,
            "FUNCTIONPOOL_INFO_NAMES 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_INFO_NAME("/guard_func").m_status == YomkResponse::eInvalid,
            "FUNCTIONPOOL_INFO_NAME 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::FUNCTIONPOOL_INFO_ALL().m_status == YomkResponse::eInvalid,
            "FUNCTIONPOOL_INFO_ALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(YomkAPI::SERVER_INFO_ALL().m_status == YomkResponse::eInvalid, "SERVER_INFO_ALL 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::SERVER_INFO_SERVICES().m_status == YomkResponse::eInvalid,
            "SERVER_INFO_SERVICES 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::SERVER_INFO_FUNCTIONS("/guard_srv").m_status == YomkResponse::eInvalid,
            "SERVER_INFO_FUNCTIONS 未初始化返回 eInvalid");
        YOMK_TEST_CHECK(
            YomkAPI::SERVER_INFO_FUNCTION("/guard_srv/f").m_status == YomkResponse::eInvalid,
            "SERVER_INFO_FUNCTION 未初始化返回 eInvalid");

        // CONTEXT_GET 特殊形态：守卫返回值即调用方默认值（指针一致）
        std::shared_ptr<yomkmsg::String> ctxDefault = std::make_shared<yomkmsg::String>("guard_default");
        std::shared_ptr<yomkmsg::String> ctxGot = YomkAPI::CONTEXT_GET<yomkmsg::String>("String", "guard_key", ctxDefault);
        YOMK_TEST_CHECK(ctxGot == ctxDefault, "CONTEXT_GET 未初始化原样返回调用方默认值");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
