/**
 * @file TestYomkContextConcurrency.cpp
 * @brief YomkContext 多线程并发功能测试与 10 万级压力测试（Context 模块 MC4）
 *
 * 覆盖内容：
 * 1. 并发 get/set 共享键（m_contextsMutex 读写锁竞争）
 * 2. 并发 create/destroy 独立键（map 结构变更）
 * 3. 并发 setMonitor 同键（修复验证：写 monitors 须 unique_lock，否则 push_back 竞争损坏）
 * 4. 并发 setChecker × keyInfo 同键（修复验证：写 checker 与读 checker/monitors 的读写竞争）
 * 5. 并发 set 触发 checker（checker 在 unique_lock 内串行调用）
 * 6. 并发 set 触发 sync + async monitor（同步计数确定、异步单线程池计数确定）
 * 7. 同步 monitor 重入安全（monitor 在锁外执行，内层 Context API 不死锁）
 * 8. N7 压力：8 线程 × 12500 ≈ 10 万级混合 Context 操作无崩溃/无挂起
 *
 * 说明：YOMK_INIT(1) 拉起单例 /YomkContext，全程经 YOMK_CONTEXT_* 宏多线程并发路由（真实 API 路径）；
 *       各 section 用独立 key 前缀，末尾 YOMK_SHUTDOWN（call_once 约束，shutdown 后不可再初始化）；
 *       书面豁免：重入 checker（checker 内调用 Context API）自死锁，属设计约束（checker 在 unique_lock 内执行），不构造该用例
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
#include <vector>

#include "YomkAPI.h"

using yomk::ContextChecker;

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

// ---- 文件级观测变量（每节首重置） ----
static std::atomic<int> g_syncMon{0};      // 同步 monitor 调用次数
static std::atomic<int> g_asyncMon{0};     // 异步 monitor 调用次数
static std::atomic<int> g_checkerCalls{0}; // checker 调用次数
static std::atomic<int> g_reentrantOk{0};  // monitor 内重入 Context API 成功次数
static std::atomic<int> g_illegal{0};      // 非法响应计数（压力段）

static void resetGlobals()
{
    g_syncMon.store(0);
    g_asyncMon.store(0);
    g_checkerCalls.store(0);
    g_reentrantOk.store(0);
    g_illegal.store(0);
}

// 轮询等待谓词成立，超时打印并返回是否达标
static bool waitFor(std::function<bool()> pred, int timeoutMs, const std::string &desc)
{
    for (int i = 0; i < timeoutMs / 10; ++i)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "[TIMEOUT] " << desc << " (timeout " << timeoutMs << " ms)" << std::endl;
    return pred();
}

int main()
{
    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    // ============ Section 1: 并发 get/set 共享键（读写锁竞争）============
    std::cout << "===== 1. 并发 get/set 共享键 =====" << std::endl;
    {
        resetGlobals();
        auto cr = YOMK_CONTEXT_CREATE("conc_rw", YomkMkPtr(String, std::string("v_init")));
        CHECK(cr.m_status == YomkResponse::eOk, "1.1 创建 conc_rw 成功");

        const int T = 8, K = 500;
        std::atomic<int> okGet{0}, okSet{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&, t]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    if (i % 2 == 0)
                    {
                        auto v = YOMK_CONTEXT_GET(String, "conc_rw", YomkMkPtr(String, std::string("")));
                        // conc_rw 始终存在且值为非空 String，命中 eOk 路径返回非空值
                        if (v && !v->d.empty())
                            ++okGet;
                        else
                            ++g_illegal;
                    }
                    else
                    {
                        auto r = YOMK_CONTEXT_SET("conc_rw", YomkMkPtr(String, std::string("v") + std::to_string(t) + "_" + std::to_string(i)));
                        if (r.m_status == YomkResponse::eOk)
                            ++okSet;
                        else
                            ++g_illegal;
                    }
                } });
        }
        for (auto &th : workers)
            th.join();

        int expectEach = T * (K / 2);
        CHECK(okGet.load() == expectEach, "1.2 并发 get 全部 eOk");
        CHECK(okSet.load() == expectEach, "1.3 并发 set 全部 eOk");
        CHECK(g_illegal.load() == 0, "1.4 无非法响应");
    }

    // ============ Section 2: 并发 create/destroy 独立键（map 结构变更）============
    std::cout << "===== 2. 并发 create/destroy 独立键 =====" << std::endl;
    {
        resetGlobals();
        const int T = 8, K = 200;
        std::atomic<int> okCreate{0}, okDestroy{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&, t]
                                 {
                std::string key = "conc_cd_" + std::to_string(t);
                for (int i = 0; i < K; ++i)
                {
                    auto c = YOMK_CONTEXT_CREATE(key, YomkMkPtr(String, std::string("x")));
                    if (c.m_status == YomkResponse::eOk)
                        ++okCreate;
                    else
                        ++g_illegal;
                    auto d = YOMK_CONTEXT_DESTROY(key);
                    if (d.m_status == YomkResponse::eOk)
                        ++okDestroy;
                    else
                        ++g_illegal;
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okCreate.load() == T * K, "2.1 并发 create 全部 eOk");
        CHECK(okDestroy.load() == T * K, "2.2 并发 destroy 全部 eOk");
        CHECK(g_illegal.load() == 0, "2.3 无非法响应");

        // 各专属键最终均已销毁（无泄漏）
        bool allGone = true;
        for (int t = 0; t < T; ++t)
        {
            auto r = YOMK_CONTEXT_INFO_KEY("conc_cd_" + std::to_string(t));
            if (r.m_status != YomkResponse::eNo)
                allGone = false;
        }
        CHECK(allGone, "2.4 独立键全部销毁，keys 无残留");
    }

    // ============ Section 3: 并发 setMonitor 同键（修复验证）============
    std::cout << "===== 3. 并发 setMonitor 同键 =====" << std::endl;
    {
        resetGlobals();
        auto cr = YOMK_CONTEXT_CREATE("conc_mon", YomkMkPtr(String, std::string("m0")));
        CHECK(cr.m_status == YomkResponse::eOk, "3.1 创建 conc_mon 成功");
        YOMK_CONTEXT_ON_MONITOR();

        const int T = 8, K = 100; // 共 T*K 个同步 monitor 并发 push_back
        std::atomic<int> okMon{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    auto r = YOMK_CONTEXT_SET_MONITOR("conc_mon", [](const yomk::Context &)
                                                      { ++g_syncMon; }, /*async=*/false);
                    if (r.m_status == YomkResponse::eOk)
                        ++okMon;
                    else
                        ++g_illegal;
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okMon.load() == T * K, "3.2 并发 setMonitor 全部 eOk");
        CHECK(g_illegal.load() == 0, "3.3 无非法响应");

        // 单次 set 触发全部已注册 monitor：计数 == 注册次数，证明并发 push_back 无丢失/损坏
        g_syncMon.store(0);
        auto sr = YOMK_CONTEXT_SET("conc_mon", YomkMkPtr(String, std::string("m1")));
        CHECK(sr.m_status == YomkResponse::eOk, "3.4 触发 set 返回 eOk");
        CHECK(g_syncMon.load() == T * K, "3.5 并发注册的 monitor 全部落地（计数 == 注册次数）");
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 4: 并发 setChecker × keyInfo 同键（修复验证读写竞争）============
    std::cout << "===== 4. 并发 setChecker × keyInfo 同键 =====" << std::endl;
    {
        resetGlobals();
        auto cr = YOMK_CONTEXT_CREATE("conc_ck", YomkMkPtr(String, std::string("c0")));
        CHECK(cr.m_status == YomkResponse::eOk, "4.1 创建 conc_ck 成功");

        const int T = 16, K = 100; // 半数写 checker、半数读 keyInfo
        std::atomic<int> okWriter{0}, okReader{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            bool writer = (t % 2 == 0);
            workers.emplace_back([&, writer]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    if (writer)
                    {
                        auto r = YOMK_CONTEXT_SET_CHECKER("conc_ck", [](const yomk::Context &)
                                                          { return ContextChecker::eAccept; });
                        if (r.m_status == YomkResponse::eOk)
                            ++okWriter;
                        else
                            ++g_illegal;
                    }
                    else
                    {
                        auto r = YOMK_CONTEXT_INFO_KEY("conc_ck");
                        if (r.m_status == YomkResponse::eOk)
                            ++okReader;
                        else
                            ++g_illegal;
                    }
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okWriter.load() == (T / 2) * K, "4.2 并发 setChecker 全部 eOk");
        CHECK(okReader.load() == (T / 2) * K, "4.3 并发 keyInfo 全部 eOk");
        CHECK(g_illegal.load() == 0, "4.4 读写并发无非法响应、无崩溃");

        // 并发注册后 checker 仍有效：开启 checker 后单次 set 调用 checker 一次
        YOMK_CONTEXT_ON_CHECKER();
        g_checkerCalls.store(0);
        YOMK_CONTEXT_SET_CHECKER("conc_ck", [](const yomk::Context &)
                                 {
            ++g_checkerCalls;
            return ContextChecker::eAccept; });
        auto sr = YOMK_CONTEXT_SET("conc_ck", YomkMkPtr(String, std::string("c1")));
        CHECK(sr.m_status == YomkResponse::eOk, "4.5 并发注册后 set 仍 eOk");
        CHECK(g_checkerCalls.load() == 1, "4.6 checker 有效并被调用 1 次");
        YOMK_CONTEXT_OFF_CHECKER();
    }

    // ============ Section 5: 并发 set 触发 checker ============
    std::cout << "===== 5. 并发 set 触发 checker =====" << std::endl;
    {
        resetGlobals();
        auto cr = YOMK_CONTEXT_CREATE("conc_setck", YomkMkPtr(String, std::string("s0")));
        CHECK(cr.m_status == YomkResponse::eOk, "5.1 创建 conc_setck 成功");
        YOMK_CONTEXT_SET_CHECKER("conc_setck", [](const yomk::Context &)
                                 {
            ++g_checkerCalls;
            return ContextChecker::eAccept; });
        YOMK_CONTEXT_ON_CHECKER();

        const int T = 8, K = 200;
        std::atomic<int> okSet{0};
        g_checkerCalls.store(0);
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    auto r = YOMK_CONTEXT_SET("conc_setck", YomkMkPtr(String, std::string("s") + std::to_string(i)));
                    if (r.m_status == YomkResponse::eOk)
                        ++okSet;
                    else
                        ++g_illegal;
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okSet.load() == T * K, "5.2 并发 set 全部 eOk");
        CHECK(g_checkerCalls.load() == T * K, "5.3 checker 调用次数 == set 次数（unique_lock 内串行）");
        CHECK(g_illegal.load() == 0, "5.4 无非法响应");
        YOMK_CONTEXT_OFF_CHECKER();
    }

    // ============ Section 6: 并发 set 触发 sync + async monitor ============
    std::cout << "===== 6. 并发 set 触发 sync+async monitor =====" << std::endl;
    {
        resetGlobals();
        auto cr = YOMK_CONTEXT_CREATE("conc_setmon", YomkMkPtr(String, std::string("n0")));
        CHECK(cr.m_status == YomkResponse::eOk, "6.1 创建 conc_setmon 成功");
        YOMK_CONTEXT_SET_MONITOR("conc_setmon", [](const yomk::Context &)
                                 { ++g_syncMon; }, /*async=*/false);
        YOMK_CONTEXT_SET_MONITOR("conc_setmon", [](const yomk::Context &)
                                 { ++g_asyncMon; }, /*async=*/true);
        YOMK_CONTEXT_ON_MONITOR();

        const int T = 8, K = 200;
        std::atomic<int> okSet{0};
        g_syncMon.store(0);
        g_asyncMon.store(0);
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    auto r = YOMK_CONTEXT_SET("conc_setmon", YomkMkPtr(String, std::string("n") + std::to_string(i)));
                    if (r.m_status == YomkResponse::eOk)
                        ++okSet;
                    else
                        ++g_illegal;
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okSet.load() == T * K, "6.2 并发 set 全部 eOk");
        CHECK(g_syncMon.load() == T * K, "6.3 同步 monitor 计数 == set 次数");
        bool asyncOk = waitFor([&]
                               { return g_asyncMon.load() == T * K; }, 10000, "async monitor count");
        CHECK(asyncOk, "6.4 异步 monitor 计数 == set 次数（单线程池排空）");
        CHECK(g_illegal.load() == 0, "6.5 无非法响应");
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 7: 同步 monitor 重入安全（锁外执行正面验证）============
    std::cout << "===== 7. 同步 monitor 重入安全 =====" << std::endl;
    {
        resetGlobals();
        auto cra = YOMK_CONTEXT_CREATE("conc_a", YomkMkPtr(String, std::string("a0")));
        auto crb = YOMK_CONTEXT_CREATE("conc_b", YomkMkPtr(String, std::string("b0")));
        CHECK(cra.m_status == YomkResponse::eOk && crb.m_status == YomkResponse::eOk, "7.1 创建 conc_a/conc_b 成功");

        // conc_a 的同步 monitor 内重入 Context API（读 conc_b）：set 已在锁外执行 monitor，不应死锁
        YOMK_CONTEXT_SET_MONITOR("conc_a", [](const yomk::Context &)
                                 {
            auto r = YOMK_CONTEXT_INFO_KEY("conc_b");
            if (r.m_status == YomkResponse::eOk)
                ++g_reentrantOk; });
        YOMK_CONTEXT_ON_MONITOR();

        g_reentrantOk.store(0);
        auto sr = YOMK_CONTEXT_SET("conc_a", YomkMkPtr(String, std::string("a1")));
        CHECK(sr.m_status == YomkResponse::eOk, "7.2 set(conc_a) 返回 eOk（monitor 重入未死锁）");
        CHECK(g_reentrantOk.load() == 1, "7.3 monitor 内重入 INFO_KEY(conc_b) 成功 1 次");
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 8: N7 压力（8 线程 × 12500 ≈ 10 万级混合操作）============
    std::cout << "===== 8. N7 压力（≈10 万级混合操作） =====" << std::endl;
    {
        resetGlobals();
        const int M = 64; // 预建 keyspace 大小
        for (int n = 0; n < M; ++n)
        {
            auto cr = YOMK_CONTEXT_CREATE("stress_" + std::to_string(n), YomkMkPtr(String, std::string("init") + std::to_string(n)));
            if (cr.m_status != YomkResponse::eOk)
                ++g_illegal;
        }
        CHECK(g_illegal.load() == 0, "8.1 预建 keyspace 成功");

        // 压力段关闭 checker/monitor：避免单线程监控池积压，聚焦锁并发与 map 一致性
        YOMK_CONTEXT_OFF_CHECKER();
        YOMK_CONTEXT_OFF_MONITOR();

        const int T = 8, K = 12500; // T*K = 100000 ≈ 10 万级
        std::atomic<int> okOps{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < T; ++t)
        {
            workers.emplace_back([&, t]
                                 {
                for (int i = 0; i < K; ++i)
                {
                    std::string key = "stress_" + std::to_string(i % M);
                    int op = i % 8;
                    switch (op)
                    {
                    case 0:
                    {
                        auto v = YOMK_CONTEXT_GET(String, key, YomkMkPtr(String, std::string("")));
                        if (v && !v->d.empty())
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 1:
                    {
                        auto r = YOMK_CONTEXT_SET(key, YomkMkPtr(String, std::string("sv")));
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 2:
                    {
                        auto r = YOMK_CONTEXT_INFO_KEYS();
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 3:
                    {
                        auto r = YOMK_CONTEXT_INFO_KEY(key);
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 4:
                    {
                        auto r = YOMK_CONTEXT_INFO_ALL();
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 5:
                    {
                        auto r = YOMK_CONTEXT_SET_CHECKER(key, [](const yomk::Context &)
                                                          { return ContextChecker::eAccept; });
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    case 6:
                    {
                        auto r = YOMK_CONTEXT_SET_MONITOR(key, [](const yomk::Context &) {});
                        if (r.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    default:
                    {
                        std::string tk = "stress_tmp_" + std::to_string(t);
                        auto c = YOMK_CONTEXT_CREATE(tk, YomkMkPtr(String, std::string("tmp")));
                        auto d = YOMK_CONTEXT_DESTROY(tk);
                        if (c.m_status == YomkResponse::eOk && d.m_status == YomkResponse::eOk)
                            ++okOps;
                        else
                            ++g_illegal;
                        break;
                    }
                    }
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(g_illegal.load() == 0, "8.2 10 万级混合操作无非法响应");
        CHECK(okOps.load() == T * K, "8.3 全部操作成功计数守恒");

        // 最终一致性：keyspace 仍存在、临时键已销毁
        bool keyspaceOk = (YOMK_CONTEXT_INFO_KEY("stress_0").m_status == YomkResponse::eOk) &&
                          (YOMK_CONTEXT_INFO_KEY("stress_" + std::to_string(M - 1)).m_status == YomkResponse::eOk);
        CHECK(keyspaceOk, "8.4 压力后 keyspace 仍存在");
        bool tmpGone = true;
        for (int t = 0; t < T; ++t)
        {
            if (YOMK_CONTEXT_INFO_KEY("stress_tmp_" + std::to_string(t)).m_status != YomkResponse::eNo)
                tmpGone = false;
        }
        CHECK(tmpGone, "8.5 压力后临时键全部销毁");
    }

    YOMK_SHUTDOWN();

    if (g_failed == 0)
    {
        std::cout << "TestYomkContextConcurrency all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkContextConcurrency FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
