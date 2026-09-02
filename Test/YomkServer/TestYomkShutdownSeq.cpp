/**
 * @file TestYomkShutdownSeq.cpp
 * @brief asyncRequest + shutdown 排空/幂等/拒新语义测试（正式全面测试，MC5）
 *
 * 覆盖内容：
 * 1. 排空语义：shutdown 阻塞至在途异步任务全部执行完毕才返回；shutdown 路径不置位
 *    markDeleted（deinit 时 deleted() == false，与 delService 删除即停对比实证）；
 *    排空期弱绑定回调照常执行
 * 2. 拒新：shutdown 后 asyncRequest 被拒（回调永不触发，日志 "async request ignored"）；
 *    shutdown 后同步 request 返回 "service not found"（现状文档化：表已清空，非专用文案）
 * 3. 幂等：连续两次 shutdown 仅一次 deinit（m_shutdown.exchange 保证不双重清理）
 * 4. 析构兜底：不显式 shutdown 直接析构，兜底触发排空与 deinit、无挂起
 * 5. 嵌套排空拒绝：排空期嵌套发起的 asyncRequest 因 m_shutdown 已置位被拒绝，不无限排空
 *
 * 说明：call_once 仅约束 YomkAPI::init 单例路径；本测试在类级直测 YomkServer::shutdown，
 *       各 YomkServer::create 实例自持独立 YomkServerPrivate::m_shutdown 互不影响，
 *       单进程多实例即可完成全部场景（API 级 YOMK_SHUTDOWN 归 MC6）；零源码变更
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <functional>
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

// ---- 文件级观测变量（每节首重置；写于池线程、读于 shutdown/析构 join 之后，无并发读写） ----
static std::atomic<int> g_taskCount{0};                  // 异步任务回调到达计数
static std::atomic<int> g_deinitCount{0};                // 服务 deinit 调用次数
static std::atomic<bool> g_deletedOnDeinit{false};       // deinit 时刻 deleted() 状态
static std::atomic<int> g_spawnExec{0};                  // /spawn 真实执行次数
static std::function<YomkResponse(YomkPkgPtr)> g_weakCb; // init 时存下的弱绑定回调
static YomkResponse g_weakRespDuringDrain;               // 排空期弱绑定回调执行结果

static void resetGlobals()
{
    g_taskCount.store(0);
    g_deinitCount.store(0);
    g_deletedOnDeinit.store(false);
    g_spawnExec.store(0);
    g_weakCb = nullptr;
    g_weakRespDuringDrain = YomkResponse{};
}

/**
 * @brief 排空演示服务：/ping 固定响应、/work 供弱绑定探针；deinit 记录计数与 deleted() 状态
 */
class DrainSrv : public YomkService
{
public:
    DrainSrv(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        YomkInstallFunc("/ping", DrainSrv::ping);
        YomkInstallFunc("/work", DrainSrv::work);
        // 存弱绑定回调至全局：排空期探针调用，验证 shutdown 不置注销标志（回调照常执行）
        g_weakCb = YomkBindWeakSelf(DrainSrv::work);
        return 0;
    }

    virtual void deinit() override
    {
        g_deinitCount.fetch_add(1);
        g_deletedOnDeinit.store(deleted()); // shutdown 排空语义应为 false（对比 delService 的 true）
    }

private:
    YomkResponse ping(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "pong"};
    }
    YomkResponse work(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "work"};
    }
};

/**
 * @brief 嵌套投递演示服务：/spawn 执行时嵌套发起自身异步请求
 *
 * 休眠 200ms 保证嵌套投递发生在主线程进入 shutdown 之后的排空期（拒绝路径）；
 * 守护上限 50 保证即使拒绝承诺失效也不至无限循环挂起，失败形态为断言
 */
class SpawnSrv : public YomkService
{
public:
    SpawnSrv(YomkServer *server)
        : YomkService(server), m_server(server)
    {
    }

public:
    virtual int init() override
    {
        YomkInstallFunc("/spawn", SpawnSrv::spawn);
        return 0;
    }

private:
    YomkResponse spawn(YomkPkgPtr pkg)
    {
        g_spawnExec.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (g_spawnExec.load() < 50 && m_server)
        {
            m_server->asyncRequest("/SpawnSrv/spawn");
        }
        return {YomkResponse::eOk, "spawn"};
    }

    YomkServer *m_server;
};

// 1. 排空语义：shutdown 阻塞至在途任务全部执行；不置注销标志；排空期弱绑定照常执行
static void testDrainSemantics()
{
    auto server = YomkServer::create(2);
    resetGlobals();
    CHECK(server->newService<DrainSrv>("/DrainSrv") == 0, "排空用例：注册服务成功");

    std::atomic<bool> probed{false};
    for (int i = 0; i < 100; ++i)
    {
        server->asyncRequest("/DrainSrv/ping", nullptr, [&probed](YomkResponse resp)
                             {
            g_taskCount.fetch_add(1);
            if (!probed.exchange(true))
            {
                // 首个回调内探针弱绑定：排空期服务未置注销标志，回调应照常执行
                g_weakRespDuringDrain = g_weakCb(nullptr);
            } });
    }

    server->shutdown(); // 阻塞至请求池排空 join 完成

    CHECK(g_taskCount.load() == 100, "排空：100 个在途异步任务在 shutdown 返回前全部执行");
    CHECK(g_deinitCount.load() == 1, "排空：池停止后服务 deinit 被调用一次");
    CHECK(g_deletedOnDeinit.load() == false,
          "排空语义：deinit 时 deleted() 为 false（shutdown 不置 markDeleted，与删除即停对比）");
    CHECK(probed.load() == true, "排空：弱绑定探针回调已执行");
    CHECK(g_weakRespDuringDrain.m_status == YomkResponse::eOk && g_weakRespDuringDrain.m_msg == "work",
          "排空期弱绑定回调照常执行（对比 delService 后丢弃）");
}

// 2. 拒新：shutdown 后异步投递被拒、回调永不触发；同步 request 现状文档化
static void testRejectAfterShutdown()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<DrainSrv>("/DrainSrv") == 0, "拒新用例：注册服务成功");
    server->shutdown();

    std::atomic<int> rejectCbCount{0};
    server->asyncRequest("/DrainSrv/ping", nullptr, [&rejectCbCount](YomkResponse resp)
                         { rejectCbCount.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(rejectCbCount.load() == 0, "拒新：shutdown 后 asyncRequest 回调永不触发（日志 async request ignored 实证）");

    YomkResponse resp = server->request("/DrainSrv/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("service not found") != std::string::npos,
          "现状文档化：shutdown 后同步 request 返回 eNo + service not found（表已清空，非专用文案）");
    CHECK(server->serviceNames().empty(), "shutdown 后服务表已清空");
}

// 3. 幂等：连续两次 shutdown 仅一次 deinit，无挂起无崩溃
static void testShutdownIdempotent()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<DrainSrv>("/DrainSrv") == 0, "幂等用例：注册服务成功");

    server->shutdown();
    server->shutdown(); // 第二次：m_shutdown.exchange 已置位直接返回

    CHECK(g_deinitCount.load() == 1, "幂等：连续两次 shutdown 仅一次 deinit（不双重清理）");
}

// 4. 析构兜底：不显式 shutdown 直接离开作用域，兜底触发排空与 deinit、无挂起
static void testDestructorFallback()
{
    resetGlobals();
    {
        auto server = YomkServer::create(1);
        server->newService<DrainSrv>("/DrainSrv");
        for (int i = 0; i < 20; ++i)
        {
            server->asyncRequest("/DrainSrv/ping", nullptr, [](YomkResponse resp)
                                 { g_taskCount.fetch_add(1); });
        }
        // 不显式 shutdown 直接离开作用域：~YomkServerPrivate 兜底触发 shutdown；
        // 在途任务持有 YomkServerPrivate 的 shared_ptr 副本，实际析构可能延迟至
        // 最后一个任务在 worker 线程释放（此时自 join 防护生效），故用超时等待观测
    }

    for (int i = 0; i < 200 && (g_taskCount.load() < 20 || g_deinitCount.load() < 1); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(g_taskCount.load() == 20, "析构兜底：析构前投递的 20 个任务同样排空执行");
    CHECK(g_deinitCount.load() == 1, "析构兜底：服务 deinit 被调用（无挂起，用例完成即证）");
}

// 5. 嵌套排空拒绝：排空期嵌套投递被 m_shutdown 拒绝，不无限排空
static void testNestedSpawnRejected()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<SpawnSrv>("/SpawnSrv") == 0, "嵌套排空用例：注册 SpawnSrv 成功");

    server->asyncRequest("/SpawnSrv/spawn");
    server->shutdown(); // /spawn 休眠 200ms 后嵌套投递，此刻 m_shutdown 已置位应被拒绝

    CHECK(g_spawnExec.load() == 1, "嵌套排空拒绝：/spawn 仅执行一次（排空期嵌套投递被拒绝，不无限排空）");
}

int main()
{
    testDrainSemantics();
    testRejectAfterShutdown();
    testShutdownIdempotent();
    testDestructorFallback();
    testNestedSpawnRejected();

    if (g_failed > 0)
    {
        std::cout << "TestYomkShutdownSeq failed, count: " << g_failed << std::endl;
        return 1;
    }
    std::cout << "TestYomkShutdownSeq all check passed." << std::endl;
    return 0;
}
