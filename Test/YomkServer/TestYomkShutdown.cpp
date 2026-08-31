/**
 * @file TestYomkShutdown.cpp
 * @brief YomkServer 优雅关闭（shutdown）测试
 *
 * 验证内容：
 * 1. shutdown() 逐个服务调用 deinit()：init 中启动的 joinable 后台线程被安全停止
 * 2. shutdown() 幂等：重复调用不崩溃、不重复 deinit
 * 3. shutdown() 后 request 返回 service not found、serviceNames() 为空
 * 4. 析构兜底：未显式 shutdown 直接销毁服务器，服务 deinit 仍被调用且不崩溃
 * 5. YOMK_SHUTDOWN 宏路径：单例关闭后 serverInstance() 为空
 * 6. 异步线程池（第二轮）：排空验证、并发有界、关闭后拒绝、异常防护、关闭期嵌套拒绝
 * 7. 编译回归防点（第四轮）：YOMK_ERR_POS_LOG 语句宏可在无大括号 if/else 中安全使用
 * 8. 同名替换（第五轮）：覆盖注册同名服务时旧服务被锁外 deinit 恰好一次
 * 9. 并发回归防点（第六轮）：单例高频快照读与 YOMK_SHUTDOWN 并发无竞态
 *
 * 独立实例用例使用 std::make_shared<YomkServer>()，避免污染 YomkAPI 单例状态
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "YomkAPI.h"

// 编译回归防点：语句宏须可在无大括号 if/else 中安全使用（仅编译验证，不调用）；
// 未包裹 do-while(0) 的裸语句宏在此写法下产生悬垂 else，编译失败
[[maybe_unused]] static void macroSafetyCheck(bool cond)
{
    if (cond)
        YOMK_ERR_POS_LOG("macro safety check: true branch.");
    else
        YOMK_ERR_POS_LOG("macro safety check: false branch.");
}

// 记录 ThreadedService::deinit 被调用的次数（独立实例用例与析构兜底用例共用）
static std::atomic<int> g_deinitCount{0};

/**
 * @brief 测试服务：init 中启动 joinable 后台线程，deinit 中停止并 join
 *
 * 若退出路径绕过 deinit（修复前的问题），线程对象随析构触发 std::terminate，
 * 进程直接崩溃 —— 本测试能正常退出即证明关闭路径生效
 */
class ThreadedService : public YomkService
{
public:
    ThreadedService(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        m_stop.store(false);
        m_worker = std::thread([this]()
                               {
            while (!m_stop.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } });
        return 0;
    }

    virtual void deinit() override
    {
        m_stop.store(true);
        if (m_worker.joinable())
        {
            m_worker.join();
        }
        g_deinitCount.fetch_add(1);
    }

private:
    std::atomic<bool> m_stop{false};
    std::thread m_worker;
};

/**
 * @brief 轻量测试服务：提供 /ping 供异步请求打到真实功能函数，避免 service not found 日志噪声
 */
class PingService : public YomkService
{
public:
    PingService(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        YomkInstallFunc("/ping", PingService::ping);
        return 0;
    }

private:
    YomkResponse ping(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "pong"};
    }
};

int main(int argc, char *argv[])
{
    int failed = 0;

    // 用例 1/2/3：独立实例显式 shutdown —— deinit 生效、幂等、关闭后拒绝请求
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<ThreadedService>("/ThreadedService");

        // 关闭前：服务在表中可见
        if (server->serviceNames().size() != 1)
        {
            std::cout << "[FAIL] before shutdown: serviceNames size != 1" << std::endl;
            failed++;
        }

        // 显式关闭：后台线程应被 deinit 停止并 join
        server->shutdown();
        if (g_deinitCount.load() != 1)
        {
            std::cout << "[FAIL] shutdown did not call deinit exactly once, count: " << g_deinitCount.load() << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] shutdown called service deinit, background thread stopped." << std::endl;
        }

        // 幂等：重复关闭不应重复 deinit、不崩溃
        server->shutdown();
        if (g_deinitCount.load() != 1)
        {
            std::cout << "[FAIL] shutdown not idempotent, deinit count: " << g_deinitCount.load() << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] shutdown is idempotent." << std::endl;
        }

        // 关闭后：请求返回 service not found，服务表为空
        YomkResponse resp = server->request("/ThreadedService/any_func", nullptr);
        if (resp.m_status != YomkResponse::eNo)
        {
            std::cout << "[FAIL] request after shutdown should be eNo, got: " << resp.m_status << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] request after shutdown rejected: " << resp.m_msg << std::endl;
        }

        if (!server->serviceNames().empty())
        {
            std::cout << "[FAIL] serviceNames after shutdown should be empty." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] serviceNames empty after shutdown." << std::endl;
        }
    }

    // 用例 4：析构兜底 —— 未显式 shutdown，直接销毁服务器也应先 deinit
    {
        int before = g_deinitCount.load();
        auto server = std::make_shared<YomkServer>();
        server->newService<ThreadedService>("/ThreadedService");
        server.reset(); // 未调用 shutdown，走 ~YomkServerPrivate 兜底

        if (g_deinitCount.load() != before + 1)
        {
            std::cout << "[FAIL] destructor fallback did not call deinit." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] destructor fallback called deinit, no terminate." << std::endl;
        }
    }

    // 用例 5：异常防护 —— 回调抛异常不终止进程，其后任务仍正常执行（独立实例）
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<PingService>("/PingService");

        std::atomic<bool> afterOk{false};
        server->asyncRequest("/PingService/ping", nullptr, [](YomkResponse response)
                             { throw std::runtime_error("test exception in async callback"); });
        // 留出时间让抛异常任务先被执行，再提交后续任务验证池未被异常拖死
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        server->asyncRequest("/PingService/ping", nullptr, [&afterOk](YomkResponse response)
                             { afterOk.store(true); });
        for (int i = 0; i < 100 && !afterOk.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!afterOk.load())
        {
            std::cout << "[FAIL] task after throwing callback did not execute." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] callback exception caught, following tasks still execute." << std::endl;
        }
        server->shutdown();
    }

    // 用例 6：排空验证 + 关闭后拒绝 —— 100 个异步请求在 shutdown 返回前全部完成，关闭后提交被拒绝
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<PingService>("/PingService");

        std::atomic<int> doneCount{0};
        for (int i = 0; i < 100; ++i)
        {
            server->asyncRequest("/PingService/ping", nullptr, [&doneCount](YomkResponse response)
                                 {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                doneCount.fetch_add(1); });
        }
        server->shutdown();
        if (doneCount.load() != 100)
        {
            std::cout << "[FAIL] drain incomplete on shutdown return, done: " << doneCount.load() << "/100" << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] all 100 async requests drained before shutdown returned." << std::endl;
        }

        // 关闭后提交：回调永不执行，无崩溃
        std::atomic<bool> lateCalled{false};
        server->asyncRequest("/PingService/ping", nullptr, [&lateCalled](YomkResponse response)
                             { lateCalled.store(true); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (lateCalled.load())
        {
            std::cout << "[FAIL] async request after shutdown should be rejected." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] async request after shutdown rejected." << std::endl;
        }
    }

    // 用例 7：并发有界 —— 同时执行的任务数不超过池线程数（硬件并发数一半向上取整，兜底 2）
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<PingService>("/PingService");

        std::atomic<int> concurrent{0};
        std::atomic<int> peak{0};
        for (int i = 0; i < 50; ++i)
        {
            server->asyncRequest("/PingService/ping", nullptr, [&concurrent, &peak](YomkResponse response)
                                 {
                int cur = concurrent.fetch_add(1) + 1;
                int p = peak.load();
                while (cur > p && !peak.compare_exchange_weak(p, cur))
                {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                concurrent.fetch_sub(1); });
        }
        server->shutdown();

        unsigned int hw = std::thread::hardware_concurrency();
        int expectMax = (hw == 0) ? 2 : static_cast<int>((hw + 1) / 2);
        if (peak.load() > expectMax)
        {
            std::cout << "[FAIL] concurrent peak " << peak.load() << " exceeds pool size " << expectMax << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] async concurrency bounded, peak: " << peak.load() << " <= pool size: " << expectMax << std::endl;
        }
    }

    // 用例 8：关闭期嵌套拒绝 —— 排空阶段内嵌套发起的异步请求被拒绝，排空不死循环
    {
        auto server = std::make_shared<YomkServer>();
        server->newService<PingService>("/PingService");

        std::atomic<bool> nestedExecuted{false};
        server->asyncRequest("/PingService/ping", nullptr, [&server, &nestedExecuted](YomkResponse response)
                             {
            // 睡到 shutdown 已置位 m_shutdown 后再嵌套发起，预期被拒绝（回调不会执行）
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            server->asyncRequest("/PingService/ping", nullptr, [&nestedExecuted](YomkResponse response2)
                                 { nestedExecuted.store(true); }); });
        server->shutdown();

        if (nestedExecuted.load())
        {
            std::cout << "[FAIL] nested async request during shutdown should be rejected." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] nested async request during shutdown rejected, drain no infinite loop." << std::endl;
        }
    }

    // 用例 9：YOMK_SHUTDOWN 宏路径 —— 单例关闭后 serverInstance 为空（含并发快照读回归防点，第六轮）
    {
        YOMK_INIT();
        if (!YOMK_SERVER_PTR)
        {
            std::cout << "[FAIL] server instance null after YOMK_INIT." << std::endl;
            failed++;
        }

        // 并发回归防点：4 线程高频取单例快照并使用，主线程随后 YOMK_SHUTDOWN；
        // 修复前该阶段为对同一 shared_ptr 的无同步读写（UB），互斥快照后安全
        std::atomic<bool> stop{false};
        std::vector<std::thread> readers;
        for (int i = 0; i < 4; ++i)
        {
            readers.emplace_back([&stop]()
                                 {
                while (!stop.load())
                {
                    auto s = YOMK_SERVER_PTR;
                    if (s)
                    {
                        s->serviceNames();
                    }
                } });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        YOMK_SHUTDOWN();
        stop.store(true);
        for (auto &t : readers)
        {
            t.join();
        }
        std::cout << "[OK] concurrent snapshot reads survived YOMK_SHUTDOWN." << std::endl;
        if (YOMK_SERVER_PTR)
        {
            std::cout << "[FAIL] server instance should be null after YOMK_SHUTDOWN." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] YOMK_SHUTDOWN released singleton." << std::endl;
        }

        // 幂等：重复关闭不崩溃
        YOMK_SHUTDOWN();
        std::cout << "[OK] YOMK_SHUTDOWN is idempotent." << std::endl;
    }

    // 用例 10：同名替换 —— 旧服务被锁外 deinit 恰好一次，新服务在表可用（第五轮）
    {
        auto server = std::make_shared<YomkServer>();
        int base = g_deinitCount.load();
        server->newService<ThreadedService>("/ThreadedService");
        server->newService<ThreadedService>("/ThreadedService"); // 同名替换：旧服务应被 deinit

        if (g_deinitCount.load() - base != 1 || server->serviceNames().size() != 1)
        {
            std::cout << "[FAIL] replace: old service deinit not exactly once or table size != 1." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] replace: old service deinited exactly once, new service in table." << std::endl;
        }

        // 关闭再 deinit 新服务一次，累计 2；重复关闭不增长（幂等已在用例 2 验证）
        server->shutdown();
        if (g_deinitCount.load() - base != 2)
        {
            std::cout << "[FAIL] replace: shutdown after replace should deinit new service once." << std::endl;
            failed++;
        }
        else
        {
            std::cout << "[OK] replace: shutdown deinited new service once." << std::endl;
        }
    }

    std::cout << (failed == 0 ? "TestYomkShutdown PASS" : "TestYomkShutdown FAIL") << std::endl;
    return failed == 0 ? 0 : 1;
}
