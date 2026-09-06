/**
 * @file TestYomkFunctionPoolStress.cpp
 * @brief YomkFunctionPool 压力测试（FPC4）
 *
 * 覆盖测试要求第 7 类（压力/吞吐/大数据量）：
 * - S1：单线程 register + call 热路径 + unregister 吞吐
 * - S2：多线程并发 call 吞吐（8 线程，shared_lock 争用观察）
 * - S3：全生命周期 churn（register→call→unregister→call(eNo) × N）
 * - S4：超大函数名压力（65536 字节）
 * - S5：API 端到端混合负载（register+call+introspect+unregister 交错）
 *
 * 规模经 YOMK_TEST_STRESS_SCALE 环境变量参数化（缺省 100000，clamp 下限 1000）。
 * 每 Section 记录 [BASELINE] 吞吐（ops/s）但不断言 timing（环境相关），仅断言守恒与正确性。
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
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
// 压力规模参数化
// ============================================================================
static uint64_t stressScale()
{
    const char *env = std::getenv("YOMK_TEST_STRESS_SCALE");
    uint64_t n = env ? std::strtoull(env, nullptr, 10) : 100000;
    return n < 1000 ? 1000 : n;
}

// ============================================================================
// [BASELINE] 记录辅助
// ============================================================================
static void recordBaseline(const char *section, uint64_t ops, double elapsedMs)
{
    double opsPerSec = (elapsedMs > 0.0) ? (ops / (elapsedMs / 1000.0)) : 0.0;
    std::cout << "[BASELINE] " << section << ": "
              << std::fixed << std::setprecision(0) << opsPerSec << " ops/s ("
              << ops << " ops in " << std::setprecision(1) << elapsedMs << " ms)"
              << std::endl;
}

// ============================================================================
// 文件级观测装置
// ============================================================================
static std::atomic<uint64_t> g_s2ExecCount{0};

// ============================================================================
// 测试用函数
// ============================================================================
static YomkResponse stressNoopFunc(YomkPkgPtr /*pkg*/)
{
    return YomkResponse(YomkResponse::eOk, "ok");
}

static YomkResponse stressCountFunc(YomkPkgPtr /*pkg*/)
{
    g_s2ExecCount.fetch_add(1, std::memory_order_relaxed);
    return YomkResponse(YomkResponse::eOk, "ok");
}

// ============================================================================
// 辅助：生成带零填充的唯一函数名
// ============================================================================
static std::string makeName(const char *prefix, uint64_t index)
{
    return std::string(prefix) + std::to_string(index);
}

// ============================================================================
// main
// ============================================================================
int main(int /*argc*/, char ** /*argv*/)
{
    std::cout << "=== TestYomkFunctionPoolStress (FPC4) ===" << std::endl;

    const uint64_t N = stressScale();
    std::cout << "[CONFIG] YOMK_TEST_STRESS_SCALE = " << N << std::endl;

    YOMK_INIT(1);

    // ========================================================================
    // Section 1：单线程 register + call 热路径 + unregister 吞吐
    // ========================================================================
    std::cout << "\n--- S1: 单线程 register/call/unregister 吞吐 ---" << std::endl;
    {
        // Phase A: register N unique names
        uint64_t regOk = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < N; ++i)
        {
            auto resp = YOMK_FUNCTIONPOOL_REGISTER(makeName("s1_func_", i), stressNoopFunc);
            if (resp.m_status == YomkResponse::eOk)
                ++regOk;
        }
        auto t1 = std::chrono::steady_clock::now();
        double regMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        recordBaseline("S1_register", N, regMs);

        CHECK(regOk == N,
              "S1: register 守恒 " + std::to_string(regOk) + "/" + std::to_string(N));

        // Phase B: call one pre-registered hot function N times
        uint64_t callOk = 0;
        auto t2 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < N; ++i)
        {
            auto resp = YOMK_FUNCTIONPOOL_CALL("s1_func_0", nullptr);
            if (resp.m_status == YomkResponse::eOk)
                ++callOk;
        }
        auto t3 = std::chrono::steady_clock::now();
        double callMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        recordBaseline("S1_call_hotpath", N, callMs);

        CHECK(callOk == N,
              "S1: call 热路径守恒 " + std::to_string(callOk) + "/" + std::to_string(N));

        // Phase C: unregister N unique names
        uint64_t unregOk = 0;
        auto t4 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < N; ++i)
        {
            auto resp = YOMK_FUNCTIONPOOL_UNREGISTER(makeName("s1_func_", i));
            if (resp.m_status == YomkResponse::eOk)
                ++unregOk;
        }
        auto t5 = std::chrono::steady_clock::now();
        double unregMs = std::chrono::duration<double, std::milli>(t5 - t4).count();
        recordBaseline("S1_unregister", N, unregMs);

        CHECK(unregOk == N,
              "S1: unregister 守恒 " + std::to_string(unregOk) + "/" + std::to_string(N));
    }

    // ========================================================================
    // Section 2：多线程并发 call 吞吐
    // ========================================================================
    std::cout << "\n--- S2: 多线程并发 call 吞吐 ---" << std::endl;
    {
        constexpr int kThreads = 8;
        const uint64_t perThread = N / kThreads;
        const uint64_t totalCalls = perThread * kThreads;

        g_s2ExecCount.store(0);
        YOMK_FUNCTIONPOOL_REGISTER("s2_hot", stressCountFunc);

        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            workers.emplace_back([perThread]()
                                 {
                for (uint64_t i = 0; i < perThread; ++i)
                {
                    YOMK_FUNCTIONPOOL_CALL("s2_hot", nullptr);
                } });
        }
        for (auto &th : workers)
            th.join();
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
        recordBaseline("S2_concurrent_call", totalCalls, elapsed);

        CHECK(g_s2ExecCount.load() == totalCalls,
              "S2: 并发 call 守恒 " + std::to_string(g_s2ExecCount.load()) +
                  "/" + std::to_string(totalCalls));

        YOMK_FUNCTIONPOOL_UNREGISTER("s2_hot");
    }

    // ========================================================================
    // Section 3：全生命周期 churn（register→call→unregister→call(eNo) × N）
    // ========================================================================
    std::cout << "\n--- S3: 全生命周期 churn ---" << std::endl;
    {
        uint64_t regOk = 0, callOk = 0, unregOk = 0, postNo = 0;

        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < N; ++i)
        {
            std::string name = makeName("s3_churn_", i);
            if (YOMK_FUNCTIONPOOL_REGISTER(name, stressNoopFunc).m_status == YomkResponse::eOk)
                ++regOk;
            if (YOMK_FUNCTIONPOOL_CALL(name, nullptr).m_status == YomkResponse::eOk)
                ++callOk;
            if (YOMK_FUNCTIONPOOL_UNREGISTER(name).m_status == YomkResponse::eOk)
                ++unregOk;
            if (YOMK_FUNCTIONPOOL_CALL(name, nullptr).m_status == YomkResponse::eNo)
                ++postNo;
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
        recordBaseline("S3_churn_4ops", N * 4, elapsed);

        CHECK(regOk == N, "S3: churn register 守恒 " + std::to_string(regOk) + "/" + std::to_string(N));
        CHECK(callOk == N, "S3: churn call 守恒 " + std::to_string(callOk) + "/" + std::to_string(N));
        CHECK(unregOk == N, "S3: churn unregister 守恒 " + std::to_string(unregOk) + "/" + std::to_string(N));
        CHECK(postNo == N, "S3: churn post-unregister eNo 守恒 " + std::to_string(postNo) + "/" + std::to_string(N));
    }

    // ========================================================================
    // Section 4：超大函数名压力（65536 字节）
    // ========================================================================
    std::cout << "\n--- S4: 超大函数名压力 ---" << std::endl;
    {
        const uint64_t rounds = N / 100; // 降低轮次避免 OOM
        const std::string bigName(65536, 'X');

        uint64_t regOk = 0, callOk = 0, unregOk = 0;

        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < rounds; ++i)
        {
            if (YOMK_FUNCTIONPOOL_REGISTER(bigName, stressNoopFunc).m_status == YomkResponse::eOk)
                ++regOk;
            if (YOMK_FUNCTIONPOOL_CALL(bigName, nullptr).m_status == YomkResponse::eOk)
                ++callOk;
            if (YOMK_FUNCTIONPOOL_UNREGISTER(bigName).m_status == YomkResponse::eOk)
                ++unregOk;
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
        recordBaseline("S4_bigname_3ops", rounds * 3, elapsed);

        CHECK(regOk == rounds,
              "S4: 大名 register 守恒 " + std::to_string(regOk) + "/" + std::to_string(rounds));
        CHECK(callOk == rounds,
              "S4: 大名 call 守恒 " + std::to_string(callOk) + "/" + std::to_string(rounds));
        CHECK(unregOk == rounds,
              "S4: 大名 unregister 守恒 " + std::to_string(unregOk) + "/" + std::to_string(rounds));

        // 最终确认已清理
        CHECK(YOMK_FUNCTIONPOOL_CALL(bigName, nullptr).m_status == YomkResponse::eNo,
              "S4: 大名最终已注销（eNo）");
    }

    // ========================================================================
    // Section 5：API 端到端混合负载
    // ========================================================================
    std::cout << "\n--- S5: API 端到端混合负载 ---" << std::endl;
    {
        constexpr int kGroupSize = 10;
        const uint64_t groups = N / kGroupSize;

        uint64_t regOk = 0, callOk = 0, infoNamesOk = 0, infoAllOk = 0, unregOk = 0;

        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t g = 0; g < groups; ++g)
        {
            // register 10
            for (int k = 0; k < kGroupSize; ++k)
            {
                auto resp = YOMK_FUNCTIONPOOL_REGISTER(makeName("s5_mix_", g * kGroupSize + k), stressNoopFunc);
                if (resp.m_status == YomkResponse::eOk)
                    ++regOk;
            }
            // call 10
            for (int k = 0; k < kGroupSize; ++k)
            {
                auto resp = YOMK_FUNCTIONPOOL_CALL(makeName("s5_mix_", g * kGroupSize + k), nullptr);
                if (resp.m_status == YomkResponse::eOk)
                    ++callOk;
            }
            // introspect
            if (YOMK_FUNCTIONPOOL_INFO_NAMES().m_status == YomkResponse::eOk)
                ++infoNamesOk;
            if (YOMK_FUNCTIONPOOL_INFO_ALL().m_status == YomkResponse::eOk)
                ++infoAllOk;
            // unregister 10
            for (int k = 0; k < kGroupSize; ++k)
            {
                auto resp = YOMK_FUNCTIONPOOL_UNREGISTER(makeName("s5_mix_", g * kGroupSize + k));
                if (resp.m_status == YomkResponse::eOk)
                    ++unregOk;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
        // 每组 10+10+1+1+10 = 32 ops
        recordBaseline("S5_mixed_32ops_per_group", groups * 32, elapsed);

        const uint64_t expectedPerType = groups * kGroupSize;
        CHECK(regOk == expectedPerType,
              "S5: 混合 register 守恒 " + std::to_string(regOk) + "/" + std::to_string(expectedPerType));
        CHECK(callOk == expectedPerType,
              "S5: 混合 call 守恒 " + std::to_string(callOk) + "/" + std::to_string(expectedPerType));
        CHECK(infoNamesOk == groups,
              "S5: INFO_NAMES 守恒 " + std::to_string(infoNamesOk) + "/" + std::to_string(groups));
        CHECK(infoAllOk == groups,
              "S5: INFO_ALL 守恒 " + std::to_string(infoAllOk) + "/" + std::to_string(groups));
        CHECK(unregOk == expectedPerType,
              "S5: 混合 unregister 守恒 " + std::to_string(unregOk) + "/" + std::to_string(expectedPerType));
    }

    // ========================================================================
    // 收尾
    // ========================================================================
    YOMK_SHUTDOWN();

    std::cout << "\n=== Result: " << (g_total - g_failed) << "/" << g_total
              << " passed, " << g_failed << " failed ===" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
