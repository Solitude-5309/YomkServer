/**
 * @file TestYomkFunctionPoolConcurrency.cpp
 * @brief YomkFunctionPool 并发正确性验证（FPC3）
 *
 * 覆盖测试要求第 5/10 类（TSan 数据竞争 + 多线程并发正确性）：
 * - S1：并发 CALL 守恒（只读路径 shared_lock 并发拷贝）+ 嵌套重入（同线程二次 shared_lock）
 * - S2：并发 REGISTER 更新 × CALL（unique_lock 写 vs shared_lock 读交错，无 torn std::function）
 * - S3：并发 UNREGISTER × CALL（锁外调用安全性——erase 后在途调用仍完成）
 * - S4：并发内省 × 变更（INFO_ALL 快照一致性：functions:N == 列表行数）
 * - S5：全量 churn 混合守恒（register/update/unregister/call/introspect 全并发）
 *
 * 装置：文件级原子计数/自旋门（TSan-clean），YOMK_INIT 单例，段尾清理共享池。
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "YomkAPI.h"

// ============================================================================
// 测试基础设施
// ============================================================================
static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond, msg)                                                         \
    do                                                                           \
    {                                                                            \
        ++g_total;                                                               \
        if (!(cond))                                                             \
        {                                                                        \
            ++g_failed;                                                          \
            std::cerr << "[FAIL] " << msg << "  (" << #cond << ")" << std::endl; \
        }                                                                        \
        else                                                                     \
        {                                                                        \
            std::cout << "[PASS] " << msg << std::endl;                          \
        }                                                                        \
    } while (0)

// ============================================================================
// 文件级观测装置（TSan-clean：所有跨线程共享状态为文件级原子或互斥保护）
// ============================================================================

// S1 守恒
static std::atomic<uint64_t> g_s1ExecCount{0};
static std::atomic<uint64_t> g_s1OkCount{0};
static std::atomic<uint64_t> g_s1InnerCount{0};
static std::atomic<uint64_t> g_s1OuterOkCount{0};

// S2 守恒
static std::atomic<uint64_t> g_s2V1Count{0};
static std::atomic<uint64_t> g_s2V2Count{0};
static std::atomic<uint64_t> g_s2TotalCalls{0};
static std::atomic<bool> g_s2Stop{false};

// S3 锁外调用安全性
static std::atomic<bool> g_s3GateStarted{false};
static std::atomic<bool> g_s3GateRelease{false};
static std::atomic<uint64_t> g_s3ExecCount{0};

// S4 快照一致性
struct SnapshotRecord
{
    uint64_t declaredN;
    uint64_t actualLines;
};
static std::mutex g_s4ObsMutex;
static std::vector<SnapshotRecord> g_s4Snapshots;

// S5 churn 守恒
static std::atomic<uint64_t> g_s5CallerOk{0};
static std::atomic<uint64_t> g_s5CallerNo{0};
static std::atomic<uint64_t> g_s5CallerTotal{0};
static std::atomic<uint64_t> g_s5UnregOk{0};
static std::atomic<uint64_t> g_s5UnregNo{0};
static std::atomic<uint64_t> g_s5RegOk{0};
static std::atomic<uint64_t> g_s5IntrospectCount{0};

// ============================================================================
// S1 用函数
// ============================================================================
static YomkResponse s1ProbeFunc(YomkPkgPtr /*pkg*/)
{
    g_s1ExecCount.fetch_add(1, std::memory_order_relaxed);
    return YomkResponse(YomkResponse::eOk, "s1_ok");
}

static YomkResponse s1InnerFunc(YomkPkgPtr /*pkg*/)
{
    g_s1InnerCount.fetch_add(1, std::memory_order_relaxed);
    return YomkResponse(YomkResponse::eOk, "s1_inner_ok");
}

static YomkResponse s1OuterFunc(YomkPkgPtr /*pkg*/)
{
    // 嵌套重入：在已注册函数内部再 CALL 另一个已注册函数
    // callFunction 的 shared_lock 已释放（锁外调用），此处重新获取 shared_lock 安全
    auto resp = YOMK_FUNCTIONPOOL_CALL("c1_inner", nullptr);
    if (resp.m_status == YomkResponse::eOk)
    {
        g_s1OuterOkCount.fetch_add(1, std::memory_order_relaxed);
    }
    return resp;
}

// ============================================================================
// S2 用函数
// ============================================================================
static YomkResponse s2V1Func(YomkPkgPtr /*pkg*/)
{
    g_s2V1Count.fetch_add(1, std::memory_order_relaxed);
    return YomkResponse(YomkResponse::eOk, "v1");
}

static YomkResponse s2V2Func(YomkPkgPtr /*pkg*/)
{
    g_s2V2Count.fetch_add(1, std::memory_order_relaxed);
    return YomkResponse(YomkResponse::eOk, "v2");
}

// ============================================================================
// S3 用函数（gate 模式：函数体内自旋等待释放，制造"锁外执行中"的确定窗口）
// ============================================================================
static YomkResponse s3GatedFunc(YomkPkgPtr /*pkg*/)
{
    g_s3ExecCount.fetch_add(1, std::memory_order_relaxed);
    g_s3GateStarted.store(true, std::memory_order_release);
    // 自旋等待主线程释放 gate（此时 callFunction 的 shared_lock 已释放，
    // 本函数在锁外执行——主线程可安全获取 unique_lock 做 unregister）
    while (!g_s3GateRelease.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return YomkResponse(YomkResponse::eOk, "s3_gated_done");
}

// ============================================================================
// S5 用函数
// ============================================================================
static YomkResponse s5ProbeFunc(YomkPkgPtr /*pkg*/)
{
    return YomkResponse(YomkResponse::eOk, "s5_ok");
}

// ============================================================================
// 辅助：解析 INFO_ALL 的 StringArray 首行 "functions:N"
// ============================================================================
static uint64_t parseFunctionsN(const std::string& firstLine)
{
    // 格式: "functions:N"
    auto pos = firstLine.find("functions:");
    if (pos == std::string::npos)
        return 0;
    return std::strtoull(firstLine.c_str() + pos + 10, nullptr, 10);
}

// ============================================================================
// main
// ============================================================================
int main(int /*argc*/, char** /*argv*/)
{
    std::cout << "=== TestYomkFunctionPoolConcurrency (FPC3) ===" << std::endl;

    YOMK_INIT(1);

    // ========================================================================
    // Section 1：并发 CALL 守恒 + 嵌套重入（只读路径）
    // ========================================================================
    std::cout << "\n--- S1: 并发 CALL 守恒 + 嵌套重入 ---" << std::endl;
    {
        constexpr int kThreads = 8;
        constexpr int kCallsPerThread = 500;
        constexpr uint64_t kTotalCalls = kThreads * kCallsPerThread;

        g_s1ExecCount.store(0);
        g_s1OkCount.store(0);

        CHECK(
            YOMK_FUNCTIONPOOL_REGISTER("c1_probe", s1ProbeFunc).m_status == YomkResponse::eOk,
            "S1: 注册 c1_probe 成功");

        // 多线程并发 CALL（shared_lock 并发读 + std::function 拷贝）
        std::vector<std::thread> callers;
        callers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            callers.emplace_back(
                []()
                {
                    for (int i = 0; i < kCallsPerThread; ++i)
                    {
                        auto resp = YOMK_FUNCTIONPOOL_CALL("c1_probe", nullptr);
                        if (resp.m_status == YomkResponse::eOk)
                            g_s1OkCount.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        }
        for (auto& th : callers)
            th.join();

        CHECK(
            g_s1ExecCount.load() == kTotalCalls,
            "S1: 执行守恒 execCount == " + std::to_string(kTotalCalls) + "（实际 " +
                std::to_string(g_s1ExecCount.load()) + "）");
        CHECK(g_s1OkCount.load() == kTotalCalls, "S1: 响应守恒 okCount == " + std::to_string(kTotalCalls));

        // 嵌套重入验证
        g_s1InnerCount.store(0);
        g_s1OuterOkCount.store(0);

        CHECK(
            YOMK_FUNCTIONPOOL_REGISTER("c1_inner", s1InnerFunc).m_status == YomkResponse::eOk,
            "S1: 注册 c1_inner 成功");
        CHECK(
            YOMK_FUNCTIONPOOL_REGISTER("c1_outer", s1OuterFunc).m_status == YomkResponse::eOk,
            "S1: 注册 c1_outer（内部嵌套 CALL c1_inner）成功");

        constexpr int kNestedThreads = 4;
        constexpr int kNestedCallsPerThread = 100;
        constexpr uint64_t kNestedTotal = kNestedThreads * kNestedCallsPerThread;

        std::vector<std::thread> nestedCallers;
        nestedCallers.reserve(kNestedThreads);
        for (int t = 0; t < kNestedThreads; ++t)
        {
            nestedCallers.emplace_back(
                []()
                {
                    for (int i = 0; i < kNestedCallsPerThread; ++i)
                    {
                        YOMK_FUNCTIONPOOL_CALL("c1_outer", nullptr);
                    }
                });
        }
        for (auto& th : nestedCallers)
            th.join();

        CHECK(
            g_s1InnerCount.load() == kNestedTotal,
            "S1: 嵌套重入守恒 innerCount == " + std::to_string(kNestedTotal) + "（实际 " +
                std::to_string(g_s1InnerCount.load()) + "）——同线程二次 shared_lock 无死锁");
        CHECK(g_s1OuterOkCount.load() == kNestedTotal, "S1: 嵌套重入 outer 全部成功回传 inner 结果");

        // 清理
        YOMK_FUNCTIONPOOL_UNREGISTER("c1_probe");
        YOMK_FUNCTIONPOOL_UNREGISTER("c1_inner");
        YOMK_FUNCTIONPOOL_UNREGISTER("c1_outer");
        CHECK(
            YOMK_FUNCTIONPOOL_CALL("c1_probe", nullptr).m_status == YomkResponse::eNo, "S1: 清理后 c1_probe 已不存在");
    }

    // ========================================================================
    // Section 2：并发 REGISTER 更新 × CALL（写读交错）
    // ========================================================================
    std::cout << "\n--- S2: 并发 REGISTER 更新 × CALL ---" << std::endl;
    {
        constexpr int kCallerThreads = 6;
        constexpr int kUpdateRounds = 50;

        g_s2V1Count.store(0);
        g_s2V2Count.store(0);
        g_s2TotalCalls.store(0);
        g_s2Stop.store(false);

        CHECK(
            YOMK_FUNCTIONPOOL_REGISTER("c2_func", s2V1Func).m_status == YomkResponse::eOk,
            "S2: 注册 c2_func 为 v1 实现");

        // 启动 caller 线程（持续 CALL 直到 g_s2Stop）
        std::vector<std::thread> callers;
        callers.reserve(kCallerThreads);
        for (int t = 0; t < kCallerThreads; ++t)
        {
            callers.emplace_back(
                []()
                {
                    while (!g_s2Stop.load(std::memory_order_acquire))
                    {
                        auto resp = YOMK_FUNCTIONPOOL_CALL("c2_func", nullptr);
                        if (resp.m_status == YomkResponse::eOk)
                            g_s2TotalCalls.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        }

        // 主线程做 50 轮 REGISTER 更新交替 v1/v2
        for (int round = 0; round < kUpdateRounds; ++round)
        {
            if (round % 2 == 0)
                YOMK_FUNCTIONPOOL_REGISTER("c2_func", s2V2Func);
            else
                YOMK_FUNCTIONPOOL_REGISTER("c2_func", s2V1Func);
            // 短暂让步让 caller 线程有机会执行
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        g_s2Stop.store(true, std::memory_order_release);
        for (auto& th : callers)
            th.join();

        uint64_t v1 = g_s2V1Count.load();
        uint64_t v2 = g_s2V2Count.load();
        uint64_t total = g_s2TotalCalls.load();

        CHECK(
            v1 + v2 == total,
            "S2: 写读交错守恒 v1(" + std::to_string(v1) + ") + v2(" + std::to_string(v2) + ") == total(" +
                std::to_string(total) + ")——无 torn std::function");
        CHECK(v1 > 0, "S2: v1 实现被观测到（更新前后均有调用命中 v1）");
        CHECK(v2 > 0, "S2: v2 实现被观测到（更新确实生效）");
        CHECK(total > 0, "S2: 总调用数 > 0（caller 线程确实执行了）");

        // 清理
        YOMK_FUNCTIONPOOL_UNREGISTER("c2_func");
        CHECK(YOMK_FUNCTIONPOOL_CALL("c2_func", nullptr).m_status == YomkResponse::eNo, "S2: 清理后 c2_func 已不存在");
    }

    // ========================================================================
    // Section 3：并发 UNREGISTER × CALL（锁外调用安全性）
    // ========================================================================
    std::cout << "\n--- S3: 并发 UNREGISTER × CALL（锁外调用安全性）---" << std::endl;
    {
        g_s3GateStarted.store(false);
        g_s3GateRelease.store(false);
        g_s3ExecCount.store(0);

        CHECK(
            YOMK_FUNCTIONPOOL_REGISTER("c3_gated", s3GatedFunc).m_status == YomkResponse::eOk,
            "S3: 注册 c3_gated（gate 函数：体内自旋等待释放）");

        // 启动 caller 线程：CALL c3_gated → 进入 gate 函数 → 自旋等待
        std::thread caller([]() { YOMK_FUNCTIONPOOL_CALL("c3_gated", nullptr); });

        // 等待 gate 函数开始执行（证明 shared_lock 已释放，函数在锁外执行中）
        int waitMs = 0;
        while (!g_s3GateStarted.load(std::memory_order_acquire) && waitMs < 5000)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++waitMs;
        }
        CHECK(g_s3GateStarted.load(), "S3: gate 函数已进入执行（callFunction 的 shared_lock 已释放，锁外调用中）");

        // 在 gate 函数执行期间 UNREGISTER——unique_lock 获取成功（无 shared_lock 持有者）
        auto unregResp = YOMK_FUNCTIONPOOL_UNREGISTER("c3_gated");
        CHECK(
            unregResp.m_status == YomkResponse::eOk,
            "S3: 在途调用期间 UNREGISTER 成功（unique_lock 无阻塞——shared_lock 已释放）");

        // 释放 gate → 在途调用完成
        g_s3GateRelease.store(true, std::memory_order_release);
        caller.join();

        CHECK(
            g_s3ExecCount.load() == 1,
            "S3: 在途调用完成（erase 不影响已拷贝的 std::function——锁外调用安全性核心证明）");

        // 注销后新 CALL → eNo
        CHECK(
            YOMK_FUNCTIONPOOL_CALL("c3_gated", nullptr).m_status == YomkResponse::eNo,
            "S3: 注销后新 CALL 返回 eNo（注销生效）");
    }

    // ========================================================================
    // Section 4：并发内省 × 变更（快照一致性）
    // ========================================================================
    std::cout << "\n--- S4: 并发内省 × 变更（快照一致性）---" << std::endl;
    {
        constexpr int kChurnThreads = 3;
        constexpr int kChurnRounds = 60;
        constexpr int kIntrospectThreads = 2;
        constexpr int kIntrospectRounds = 30;

        {
            std::lock_guard<std::mutex> lk(g_s4ObsMutex);
            g_s4Snapshots.clear();
        }

        // churn 线程：并发 REGISTER/UNREGISTER 唯一名
        std::vector<std::thread> churnThreads;
        churnThreads.reserve(kChurnThreads);
        for (int t = 0; t < kChurnThreads; ++t)
        {
            churnThreads.emplace_back(
                [t]()
                {
                    for (int round = 0; round < kChurnRounds; ++round)
                    {
                        std::string name = "c4_t" + std::to_string(t) + "_" + std::to_string(round);
                        YOMK_FUNCTIONPOOL_REGISTER(name, s5ProbeFunc);
                        // 短暂让步让内省线程有机会观测
                        if (round % 10 == 9)
                            std::this_thread::sleep_for(std::chrono::microseconds(50));
                        YOMK_FUNCTIONPOOL_UNREGISTER(name);
                    }
                });
        }

        // 内省线程：轮询 INFO_ALL 并记录快照
        std::vector<std::thread> introspectThreads;
        introspectThreads.reserve(kIntrospectThreads);
        for (int t = 0; t < kIntrospectThreads; ++t)
        {
            introspectThreads.emplace_back(
                []()
                {
                    for (int round = 0; round < kIntrospectRounds; ++round)
                    {
                        auto resp = YOMK_FUNCTIONPOOL_INFO_ALL();
                        if (resp.m_status == YomkResponse::eOk && resp.m_data)
                        {
                            YomkUnPackPkg(resp.m_data, StringArray, arr);
                            if (arr && !arr->d.empty())
                            {
                                // 首行 "functions:N"，其余每行一个函数条目
                                uint64_t declaredN = parseFunctionsN(arr->d[0]);
                                uint64_t actualLines = arr->d.size() - 1;
                                std::lock_guard<std::mutex> lk(g_s4ObsMutex);
                                g_s4Snapshots.push_back({declaredN, actualLines});
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(200));
                    }
                });
        }

        for (auto& th : churnThreads)
            th.join();
        for (auto& th : introspectThreads)
            th.join();

        // 验证快照一致性
        size_t snapshotCount = 0;
        size_t inconsistentCount = 0;
        {
            std::lock_guard<std::mutex> lk(g_s4ObsMutex);
            snapshotCount = g_s4Snapshots.size();
            for (const auto& snap : g_s4Snapshots)
            {
                if (snap.declaredN != snap.actualLines)
                    ++inconsistentCount;
            }
        }

        CHECK(snapshotCount > 0, "S4: 收集到 " + std::to_string(snapshotCount) + " 个 INFO_ALL 快照");
        CHECK(
            inconsistentCount == 0,
            "S4: 全部快照一致（functions:N == 列表行数）——单次 shared_lock 内快照原子性（不一致 " +
                std::to_string(inconsistentCount) + " 个）");

        // 清理（churn 线程已自行注销，但确认池干净）
        for (int t = 0; t < kChurnThreads; ++t)
            for (int round = 0; round < kChurnRounds; ++round)
                YOMK_FUNCTIONPOOL_UNREGISTER("c4_t" + std::to_string(t) + "_" + std::to_string(round));
    }

    // ========================================================================
    // Section 5：全量 churn 混合守恒
    // ========================================================================
    std::cout << "\n--- S5: 全量 churn 混合守恒 ---" << std::endl;
    {
        constexpr int kRounds = 200;

        g_s5CallerOk.store(0);
        g_s5CallerNo.store(0);
        g_s5CallerTotal.store(0);
        g_s5UnregOk.store(0);
        g_s5UnregNo.store(0);
        g_s5RegOk.store(0);
        g_s5IntrospectCount.store(0);

        // 2 registerer 线程：反复注册/更新同名（最大互斥争用）
        std::vector<std::thread> registerers;
        for (int t = 0; t < 2; ++t)
        {
            registerers.emplace_back(
                []()
                {
                    for (int i = 0; i < kRounds; ++i)
                    {
                        auto resp = YOMK_FUNCTIONPOOL_REGISTER("c5_probe", s5ProbeFunc);
                        if (resp.m_status == YomkResponse::eOk)
                            g_s5RegOk.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        }

        // 2 caller 线程：CALL 同名（可能 eOk 或 eNo 取决于 unregister 时序）
        std::vector<std::thread> callers;
        for (int t = 0; t < 2; ++t)
        {
            callers.emplace_back(
                []()
                {
                    for (int i = 0; i < kRounds; ++i)
                    {
                        auto resp = YOMK_FUNCTIONPOOL_CALL("c5_probe", nullptr);
                        g_s5CallerTotal.fetch_add(1, std::memory_order_relaxed);
                        if (resp.m_status == YomkResponse::eOk)
                            g_s5CallerOk.fetch_add(1, std::memory_order_relaxed);
                        else if (resp.m_status == YomkResponse::eNo)
                            g_s5CallerNo.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        }

        // 1 unregistrer 线程：反复注销同名
        std::thread unregistrer(
            []()
            {
                for (int i = 0; i < kRounds; ++i)
                {
                    auto resp = YOMK_FUNCTIONPOOL_UNREGISTER("c5_probe");
                    if (resp.m_status == YomkResponse::eOk)
                        g_s5UnregOk.fetch_add(1, std::memory_order_relaxed);
                    else if (resp.m_status == YomkResponse::eNo)
                        g_s5UnregNo.fetch_add(1, std::memory_order_relaxed);
                    // 短暂让步让 registerer 有机会重新注册
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            });

        // 1 introspector 线程：INFO_NAMES 轮询
        std::thread introspector(
            []()
            {
                for (int i = 0; i < kRounds; ++i)
                {
                    auto resp = YOMK_FUNCTIONPOOL_INFO_NAMES();
                    if (resp.m_status == YomkResponse::eOk)
                        g_s5IntrospectCount.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });

        for (auto& th : registerers)
            th.join();
        for (auto& th : callers)
            th.join();
        unregistrer.join();
        introspector.join();

        uint64_t callerOk = g_s5CallerOk.load();
        uint64_t callerNo = g_s5CallerNo.load();
        uint64_t callerTotal = g_s5CallerTotal.load();
        constexpr uint64_t expectedTotal = 2 * kRounds;

        CHECK(
            callerOk + callerNo == callerTotal,
            "S5: caller 守恒 ok(" + std::to_string(callerOk) + ") + no(" + std::to_string(callerNo) + ") == total(" +
                std::to_string(callerTotal) + ")");
        CHECK(callerTotal == expectedTotal, "S5: caller 总尝试 == " + std::to_string(expectedTotal));
        CHECK(g_s5RegOk.load() == 2 * kRounds, "S5: registerer 全部成功（注册/更新幂等）");
        CHECK(
            g_s5UnregOk.load() + g_s5UnregNo.load() == kRounds,
            "S5: unregistrer 守恒 ok+no == " + std::to_string(kRounds));
        CHECK(g_s5IntrospectCount.load() > 0, "S5: introspector 成功观测到 INFO_NAMES（无崩溃）");
        CHECK(callerOk > 0, "S5: 至少部分 CALL 成功（registerer 在 unregistrer 间隙注册了函数）");

        // 清理
        YOMK_FUNCTIONPOOL_UNREGISTER("c5_probe");
    }

    // ========================================================================
    // 收尾
    // ========================================================================
    YOMK_SHUTDOWN();

    std::cout << "\n=== Result: " << (g_total - g_failed) << "/" << g_total << " passed, " << g_failed
              << " failed ===" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
