/**
 * @file TestYomkEventLoopStress.cpp
 * @brief EventLoop 压力测试（10 万级投递 + 大数据量 + QPS 基线记录）
 *
 * 覆盖内容（5 个 Section，规模 N 经 YOMK_TEST_STRESS_SCALE 环境变量参数化，缺省 100000）：
 * 1. 单线程 10 万级投递基线：post N 个 trivial 计数事件 → drain，计数守恒 + 入队/消费 QPS 基线记录
 * 2. 8 线程并发 10 万级：并发 post → drain，计数守恒 + 聚合 QPS 基线（高并发持续调用无崩溃）
 * 3. 10 万级纯积压路径：gate 占住 worker 使投递/消费速率解耦——纯测 post + 无界队列大规模增长/重分配
 *    （断言 pending:N 积压确认，释放后守恒）
 * 4. 大数据量字符串：4096 字节 tag × max(N/10,1000) 个事件（约数十 MB），无崩溃且守恒
 * 5. API 层 10 万级宏投递：YOMK_EVENTLOOP_POST 走完整路由+解包路径，计数守恒 + API 级 QPS 基线
 *
 * 说明：
 * - QPS/耗时基线仅以 [BASELINE] 行记录、不作断言（环境噪声使性能断言不可靠）；
 * - TSan 轨道守门以 YOMK_TEST_STRESS_SCALE=10000 降规模跑（大规模压力不在 TSan 下执行的既定约定，
 *   并发结构不变、多线程路径仍受 happens-before 验证）；ASan 全规模留覆盖率/sanitizer 收口闭环；
 * - 计数 handler 为文件级原子（TSan-clean 既有模式）；计时用 steady_clock；
 * - 白盒段直连内部类 EventLoop；API 段 YOMK_INIT 单例，末尾 YOMK_SHUTDOWN 收尾。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "YomkAPI.h"
#include "Modules/EventLoop/EventLoop.h"

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

// 压力规模：环境变量 YOMK_TEST_STRESS_SCALE（TSan 轨道守门降规模用），缺省 10 万，clamp 下限 1000
static size_t stressScale()
{
    const char *env = std::getenv("YOMK_TEST_STRESS_SCALE");
    if (env == nullptr || *env == '\0')
    {
        return 100000;
    }
    unsigned long v = std::strtoul(env, nullptr, 10);
    if (v < 1000)
    {
        return 1000;
    }
    return static_cast<size_t>(v);
}

// ---- 文件级观测装置（TSan-clean 既有模式）----
static std::atomic<long> g_execCount{0};

static YomkServiceFunc countHandler()
{
    return [](YomkPkgPtr)
    {
        g_execCount.fetch_add(1);
        return YomkResponse(YomkResponse::eOk, "counted");
    };
}

// ---- 文件级原子阻塞门：占住工作线程使投递/消费速率解耦 ----
static std::atomic<bool> g_gateStarted{false};
static std::atomic<bool> g_gateRelease{false};

static void resetGate()
{
    g_gateStarted.store(false);
    g_gateRelease.store(false);
}

static YomkServiceFunc gateHandler()
{
    return [](YomkPkgPtr)
    {
        g_gateStarted.store(true);
        while (!g_gateRelease.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return YomkResponse(YomkResponse::eOk, "gate done");
    };
}

static bool waitGateStarted(int timeoutMs)
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (g_gateStarted.load())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return g_gateStarted.load();
}

static void openGate()
{
    g_gateRelease.store(true);
}

// 轮询等待谓词成立，超时返回最终谓词值
static bool waitUntil(const std::function<bool()> &pred, int timeoutMs)
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (pred())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

// 基线记录（仅打印，不断言）：耗时(ms) 与吞吐(ops/s)
static void recordBaseline(const std::string &tag, size_t ops, long long elapsedMs)
{
    double qps = elapsedMs > 0 ? static_cast<double>(ops) * 1000.0 / static_cast<double>(elapsedMs) : 0.0;
    std::cout << "[BASELINE] " << tag << " ops=" << ops << " elapsed_ms=" << elapsedMs
              << " throughput=" << static_cast<long long>(qps) << "/s" << std::endl;
}

int main()
{
    const size_t N = stressScale();
    std::cout << "stress scale N = " << N << std::endl;

    // ============ Section 1: 单线程 10 万级投递基线（白盒）============
    {
        g_execCount.store(0);
        EventLoop loop;
        CHECK(loop.start() == 0, "S1 start 返回 0");

        auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k < N; ++k)
        {
            loop.post(YomkMkPtr(Event, yomk::Event("st1", nullptr, countHandler(), "")));
        }
        auto t1 = std::chrono::steady_clock::now();
        bool drained = waitUntil([&loop, N]
                                 { return g_execCount.load() == static_cast<long>(N) && loop.infoLine("st1", 0).find("pending:0") != std::string::npos; },
                                 30000);
        auto t2 = std::chrono::steady_clock::now();

        CHECK(drained, "S1 drain 完成（30s 看门狗）");
        CHECK(g_execCount.load() == static_cast<long>(N), "S1 计数守恒（N/N，无丢失无重复）");
        recordBaseline("S1 single-thread post", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        recordBaseline("S1 single-thread drain", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

        loop.destroy();
    }

    // ============ Section 2: 8 线程并发 10 万级（白盒）============
    {
        g_execCount.store(0);
        EventLoop loop;
        CHECK(loop.start() == 0, "S2 start 返回 0");

        auto t0 = std::chrono::steady_clock::now();
        {
            std::vector<std::thread> posters;
            size_t perThread = N / 8;
            for (int t = 0; t < 8; ++t)
            {
                posters.emplace_back([&loop, perThread]()
                                     {
                    for (size_t k = 0; k < perThread; ++k)
                    {
                        loop.post(YomkMkPtr(Event, yomk::Event("st2", nullptr, countHandler(), "")));
                    } });
            }
            for (auto &th : posters)
            {
                th.join();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        bool drained = waitUntil([&loop, N]
                                 { return g_execCount.load() == static_cast<long>(N) && loop.infoLine("st2", 0).find("pending:0") != std::string::npos; },
                                 30000);
        auto t2 = std::chrono::steady_clock::now();

        CHECK(drained, "S2 drain 完成（30s 看门狗）");
        CHECK(g_execCount.load() == static_cast<long>(N), "S2 并发计数守恒（8 线程 N/N）");
        recordBaseline("S2 8-thread concurrent post", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        recordBaseline("S2 8-thread drain", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

        loop.destroy();
    }

    // ============ Section 3: 10 万级纯积压路径（白盒，gate 解耦投递/消费）============
    {
        g_execCount.store(0);
        resetGate();
        EventLoop loop;
        CHECK(loop.start() == 0, "S3 start 返回 0");

        // gate 占住 worker：投递速率与消费速率解耦，纯测 post + 无界队列大规模增长/重分配
        CHECK(loop.post(YomkMkPtr(Event, yomk::Event("st3", nullptr, gateHandler(), "gate"))) == 0,
              "S3 post gate 事件返回 0");
        CHECK(waitGateStarted(2000), "S3 gate 已占住工作线程");

        auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k < N; ++k)
        {
            loop.post(YomkMkPtr(Event, yomk::Event("st3", nullptr, countHandler(), "")));
        }
        auto t1 = std::chrono::steady_clock::now();

        std::string line = loop.infoLine("st3", 0);
        CHECK(line.find("pending:" + std::to_string(N)) != std::string::npos,
              "S3 积压确认（pending:N==" + std::to_string(N) + "，无界队列大规模增长无崩溃）");

        openGate();
        bool drained = waitUntil([&loop, N]
                                 { return g_execCount.load() == static_cast<long>(N) && loop.infoLine("st3", 0).find("pending:0") != std::string::npos; },
                                 30000);
        auto t2 = std::chrono::steady_clock::now();

        CHECK(drained, "S3 释放后 drain 完成（30s 看门狗）");
        CHECK(g_execCount.load() == static_cast<long>(N), "S3 积压后计数守恒（N/N）");
        recordBaseline("S3 backlog-only post (gate held)", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        recordBaseline("S3 backlog drain after release", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

        loop.destroy();
    }

    // ============ Section 4: 大数据量字符串（白盒）============
    {
        g_execCount.store(0);
        EventLoop loop;
        CHECK(loop.start() == 0, "S4 start 返回 0");

        const size_t tagLen = 4096;
        const size_t count = N / 10 >= 1000 ? N / 10 : 1000;
        std::string bigTag(tagLen, 'T');

        auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k < count; ++k)
        {
            loop.post(YomkMkPtr(Event, yomk::Event("st4", nullptr, countHandler(), bigTag)));
        }
        auto t1 = std::chrono::steady_clock::now();
        bool drained = waitUntil([&loop, count]
                                 { return g_execCount.load() == static_cast<long>(count) && loop.infoLine("st4", 0).find("pending:0") != std::string::npos; },
                                 30000);
        auto t2 = std::chrono::steady_clock::now();

        CHECK(drained, "S4 drain 完成（30s 看门狗）");
        CHECK(g_execCount.load() == static_cast<long>(count), "S4 大数据量计数守恒（无崩溃无丢失）");
        recordBaseline("S4 big-tag post (" + std::to_string(count) + " x " + std::to_string(tagLen) + "B)",
                       count,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        recordBaseline("S4 big-tag drain", count,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

        loop.destroy();
    }

    // ============ Section 5: API 层 10 万级宏投递 ============
    {
        g_execCount.store(0);
        auto server = YOMK_INIT(1);
        CHECK(server != nullptr, "S5 YOMK_INIT 返回非空服务器");
        CHECK(YOMK_EVENTLOOP_START("stress_loop", nullptr).m_status == YomkResponse::eOk, "S5 START 返回 eOk");

        auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k < N; ++k)
        {
            YOMK_EVENTLOOP_POST("stress_loop", YomkMkPtr(String, std::string("payload")), countHandler(), "");
        }
        auto t1 = std::chrono::steady_clock::now();
        bool drained = waitUntil([N]
                                 { return g_execCount.load() == static_cast<long>(N); },
                                 30000);
        auto t2 = std::chrono::steady_clock::now();

        CHECK(drained, "S5 drain 完成（30s 看门狗）");
        CHECK(g_execCount.load() == static_cast<long>(N), "S5 API 层计数守恒（N/N，路由+解包路径不丢失）");
        recordBaseline("S5 API-level post (YOMK_EVENTLOOP_POST)", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        recordBaseline("S5 API-level end-to-end", N,
                       std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t0).count());

        CHECK(YOMK_EVENTLOOP_DESTROY("stress_loop").m_status == YomkResponse::eOk, "S5 DESTROY 返回 eOk");
        YOMK_SHUTDOWN();
    }

    if (g_failed == 0)
    {
        std::cout << "TestYomkEventLoopStress all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkEventLoopStress FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
