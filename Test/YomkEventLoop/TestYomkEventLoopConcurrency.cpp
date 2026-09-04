/**
 * @file TestYomkEventLoopConcurrency.cpp
 * @brief EventLoop 多线程并发正确性测试（白盒 + API 层）
 *
 * 覆盖内容（5 个 Section）：
 * 1. 并发 post 保序 + 计数守恒：8 线程×40 事件并发投递，执行数==320 且执行序==eventId 严格递增序
 *    （单 worker FIFO + 锁内赋号入队，赋号序=入队序=执行序三序合一；严格递增蕴含 eventId 全局唯一）
 * 2. 多 loop 并行：loopA 的 worker 被阻塞门占住期间，loopB 事件仍完成——不同 loop 独立 worker 的确定性证明
 * 3. 并发 postWait：8 线程各 10 次同步投递全部看门狗内返回、响应已填充、总数守恒
 * 4. start/stop/post/postWait 混合 churn 无死锁：固定迭代计数（非墙钟时长），收尾 drain 后精确计数守恒
 *    executed==accepted（rc==0 必已入队、stop 保留、最终执行）
 * 5. API 层并发投递 + 内省轮询：4 线程并发 POST + 1 线程并发 INFO_LOOP（服务层 shared_lock 读路径稳定性）
 *
 * 设计边界（契约内使用，非缺陷）：
 * - churn 仅单线程做 start/stop——start/stop 线程安全契约为调用方串行化（并发 start×2 竞争 m_worker
 *   move-assign 有 terminate 风险，YomkAPI.h 未声明其线程安全）；
 * - churn 不含 destroy——丢弃已接受事件会破坏计数守恒语义，其等待者释放已由 Lifecycle Section 10 覆盖。
 *
 * 说明：白盒段直连内部类 EventLoop（CMake 已含 src 目录）；API 段 YOMK_INIT 单例拉起内置 /YomkEventLoop，
 *       末尾 YOMK_SHUTDOWN 收尾。测试装置为文件级（TSan-clean 既有验证模式）：计数用原子变量，
 *       执行序列/完成标志向量用文件级互斥量且全部访问持锁（含断言快照）；阻塞门用原子自旋（1ms 粒度休眠），
 *       不用局部 mutex/cv 与 condition_variable::wait_for（本工具链 TSan 已知误报路径）；
 *       S1 的 handler 捕获事件裸指针——执行期间 run() 持有 shared_ptr 保证存活，无环引用无泄漏。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
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

// ---- 文件级观测装置（文件级生命周期稳定，TSan 下无栈槽复用串扰）----
static std::atomic<int> g_execCount{0}; // 计数 handler 执行总数（S2/S3/S4/S5）

static YomkServiceFunc countHandler()
{
    return [](YomkPkgPtr)
    {
        g_execCount.fetch_add(1);
        return YomkResponse(YomkResponse::eOk, "counted");
    };
}

// S1 专用：记录事件执行时的 eventId（handler 捕获事件裸指针，执行期间 run() 持有 shared_ptr）
static std::mutex g_idsMutex;
static std::vector<std::uint64_t> g_execIds;

static void recordExecId(std::uint64_t id)
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    g_execIds.push_back(id);
}

static std::vector<std::uint64_t> execIdsSnapshot()
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    return g_execIds;
}

static void resetExecIds()
{
    std::lock_guard<std::mutex> lk(g_idsMutex);
    g_execIds.clear();
}

// ---- 文件级原子阻塞门：占住指定 loop 的工作线程制造确定性窗口 ----
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

// S5 诊断：轮询失败样本留存（文件级，TSan-clean 模式）
static std::mutex g_pollFailMutex;
static std::vector<std::string> g_pollFailSamples;

int main()
{
    // ============ Section 1: 并发 post 保序 + 计数守恒（白盒，单 loop）============
    {
        resetExecIds();
        EventLoop loop;
        CHECK(loop.start() == 0, "S1 start 返回 0");

        std::atomic<int> postOk{0};
        auto poster = [&loop, &postOk](int)
        {
            for (int k = 0; k < 40; ++k)
            {
                auto ev = YomkMkPtr(Event, yomk::Event("conc1", nullptr, nullptr, ""));
                yomk::Event_ *raw = ev.get(); // handler 执行期间 run() 持有 shared_ptr，裸指针安全
                ev->d.m_serviceFunc = [raw](YomkPkgPtr)
                {
                    recordExecId(raw->d.m_eventId);
                    return YomkResponse(YomkResponse::eOk, "ok");
                };
                if (loop.post(ev) == 0)
                {
                    postOk.fetch_add(1);
                }
            }
        };

        std::vector<std::thread> posters;
        for (int t = 0; t < 8; ++t)
        {
            posters.emplace_back(poster, t);
        }
        for (auto &th : posters)
        {
            th.join();
        }
        CHECK(postOk.load() == 320, "S1 并发 post 全部返回 0（320/320）");

        CHECK(waitUntil([&loop]
                        { return loop.infoLine("conc1", 0).find("pending:0") != std::string::npos; },
                        5000),
              "S1 队列排空（drain 完成）");

        auto ids = execIdsSnapshot();
        CHECK(ids.size() == 320, "S1 计数守恒：执行数==投递数（320/320，无丢失无重复）");
        bool strictlyIncreasing = true;
        for (size_t i = 1; i < ids.size(); ++i)
        {
            if (ids[i] <= ids[i - 1])
            {
                strictlyIncreasing = false;
                break;
            }
        }
        CHECK(strictlyIncreasing, "S1 执行序==eventId 严格递增序（并发提交下 FIFO 保序且赋号全局唯一）");

        loop.destroy();
    }

    // ============ Section 2: 多 loop 并行（白盒，确定性）============
    {
        g_execCount.store(0);
        resetGate();
        EventLoop loopA;
        EventLoop loopB;
        CHECK(loopA.start() == 0, "S2 loopA start 返回 0");
        CHECK(loopB.start() == 0, "S2 loopB start 返回 0");

        // loopA 的 worker 被阻塞门占住
        auto gate = YomkMkPtr(Event, yomk::Event("pa", nullptr, gateHandler(), "gate"));
        CHECK(loopA.post(gate) == 0, "S2 loopA post 阻塞事件返回 0");
        CHECK(waitGateStarted(2000), "S2 阻塞门已占住 loopA 的工作线程");

        // loopB 的事件在 A 阻塞期间完成——若两 loop 共享线程则不可能，此为并行性的确定性证明
        for (int k = 0; k < 5; ++k)
        {
            CHECK(loopB.post(YomkMkPtr(Event, yomk::Event("pb", nullptr, countHandler(), ""))) == 0,
                  "S2 loopB post 计数事件返回 0");
        }
        CHECK(waitUntil([]
                        { return g_execCount.load() == 5; },
                        2000),
              "S2 loopB 事件在 loopA 阻塞期间全部完成（独立 worker 并行）");

        // 释放 loopA：gate 完成后 A 的后续事件恢复执行
        openGate();
        CHECK(loopA.post(YomkMkPtr(Event, yomk::Event("pa", nullptr, countHandler(), ""))) == 0,
              "S2 loopA post 后续计数事件返回 0");
        CHECK(waitUntil([]
                        { return g_execCount.load() == 6; },
                        2000),
              "S2 释放后 loopA 恢复执行（gate 完成+后续事件）");
        CHECK(g_execCount.load() == 6, "S2 两 loop 计数守恒（5+1==6）");

        loopA.destroy();
        loopB.destroy();
    }

    // ============ Section 3: 并发 postWait（白盒）============
    {
        g_execCount.store(0);
        EventLoop loop;
        CHECK(loop.start() == 0, "S3 start 返回 0");

        std::atomic<int> waiterDone{0}; // 完成线程数
        std::atomic<int> waitRcOk{0};   // postWait 返回 0 的次数
        std::atomic<int> waitRespOk{0}; // 返回时响应已填充 eOk 的次数

        auto waiter = [&loop, &waiterDone, &waitRcOk, &waitRespOk]()
        {
            for (int k = 0; k < 10; ++k)
            {
                auto ev = YomkMkPtr(Event, yomk::Event("conc3", nullptr, countHandler(), ""));
                int rc = loop.postWait(ev);
                if (rc == 0)
                {
                    waitRcOk.fetch_add(1);
                }
                if (ev->d.m_response.m_status == YomkResponse::eOk)
                {
                    waitRespOk.fetch_add(1);
                }
            }
            waiterDone.fetch_add(1);
        };

        std::vector<std::thread> waiters;
        for (int t = 0; t < 8; ++t)
        {
            waiters.emplace_back(waiter);
        }

        CHECK(waitUntil([&waiterDone]
                        { return waiterDone.load() == 8; },
                        5000),
              "S3 全部 8 线程看门狗内完成（postWait 无挂起）");
        for (auto &th : waiters)
        {
            th.join();
        }

        CHECK(waitRcOk.load() == 80, "S3 全部 postWait 返回 0（80/80）");
        CHECK(waitRespOk.load() == 80, "S3 全部返回时响应已填充 eOk（80/80）");
        CHECK(g_execCount.load() == 80, "S3 总执行数守恒（80/80）");
        CHECK(loop.infoLine("conc3", 0).find("pending:0") != std::string::npos, "S3 完成后 pending:0");

        loop.destroy();
    }

    // ============ Section 4: start/stop/post/postWait 混合 churn 无死锁（白盒）============
    {
        g_execCount.store(0);
        EventLoop loop;
        CHECK(loop.start() == 0, "S4 start 返回 0");

        std::atomic<int> acceptedPost{0}; // post 返回 0（已入队，必执行）
        std::atomic<int> acceptedWait{0}; // postWait 返回 0（已入队，必执行）

        auto poster = [&loop, &acceptedPost]()
        {
            for (int k = 0; k < 500; ++k)
            {
                auto ev = YomkMkPtr(Event, yomk::Event("conc4", nullptr, countHandler(), ""));
                if (loop.post(ev) == 0)
                {
                    acceptedPost.fetch_add(1);
                }
            }
        };

        auto waiter = [&loop, &acceptedWait]()
        {
            for (int k = 0; k < 100; ++k)
            {
                auto ev = YomkMkPtr(Event, yomk::Event("conc4", nullptr, countHandler(), ""));
                if (loop.postWait(ev) == 0)
                {
                    acceptedWait.fetch_add(1); // 停止态返回 2 被拒：事件未入队不计数，直接投下一个
                }
            }
        };

        // 单 churn 线程做 start/stop（契约：调用方串行化；不含 destroy——丢弃会破坏守恒语义）
        auto churner = [&loop]()
        {
            for (int r = 0; r < 40; ++r)
            {
                loop.stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                loop.start();
            }
        };

        std::vector<std::thread> workers;
        workers.emplace_back(poster);
        workers.emplace_back(poster);
        workers.emplace_back(waiter);
        workers.emplace_back(waiter);
        workers.emplace_back(churner);
        for (auto &th : workers)
        {
            th.join(); // 死锁将表现为 join 卡死，由二进制级 timeout 300 兜底判 FAIL
        }

        CHECK(loop.start() == 0, "S4 churn 结束后确保运行态（幂等）");
        int expected = acceptedPost.load() + acceptedWait.load();
        CHECK(waitUntil([&loop, expected]
                        { return g_execCount.load() == expected && loop.infoLine("conc4", 0).find("pending:0") != std::string::npos; },
                        5000),
              "S4 churn 后队列排空（drain 完成）");
        CHECK(g_execCount.load() == expected, "S4 精确计数守恒：executed==accepted（rc==0 必入队、stop 保留、最终执行）");
        CHECK(expected > 0, "S4 混合期间存在成功投递（churn 未饿死投递路径）");

        loop.destroy();
    }

    // ============ Section 5: API 层并发投递 + 内省轮询（宏）============
    {
        g_execCount.store(0);
        auto server = YOMK_INIT(1);
        CHECK(server != nullptr, "S5 YOMK_INIT 返回非空服务器");

        auto startResp = YOMK_EVENTLOOP_START("conc_loop", nullptr);
        CHECK(startResp.m_status == YomkResponse::eOk, "S5 START conc_loop 返回 eOk");

        std::atomic<int> apiPostOk{0};
        std::atomic<int> pollCount{0};
        std::atomic<int> pollOk{0};
        std::atomic<bool> stopPoll{false};

        auto apiPoster = [&apiPostOk]()
        {
            for (int k = 0; k < 25; ++k)
            {
                if (YOMK_EVENTLOOP_POST("conc_loop", YomkMkPtr(String, std::string("x")), countHandler(), "").m_status == YomkResponse::eOk)
                {
                    apiPostOk.fetch_add(1);
                }
            }
        };

        auto poller = [&pollCount, &pollOk, &stopPoll]()
        {
            // 下限 50 次：高负载下轮询线程可能在投递完成前未被调度（启动饥饿，polls=0 的环境噪声），
            // 投递完成后补足最少轮询数；轮询窗口仍覆盖并发投递期，且补足期间 worker 仍在消化队列
            while (!stopPoll.load() || pollCount.load() < 50)
            {
                auto r = YOMK_EVENTLOOP_INFO_LOOP("conc_loop");
                pollCount.fetch_add(1);
                if (r.m_status == YomkResponse::eOk && r.m_msg.find("running:on") != std::string::npos)
                {
                    pollOk.fetch_add(1);
                }
                else if (g_pollFailSamples.size() < 5) // 诊断：留存首批失败样本（非 eOk 或非 running:on）
                {
                    std::lock_guard<std::mutex> lk(g_pollFailMutex);
                    if (g_pollFailSamples.size() < 5)
                    {
                        g_pollFailSamples.push_back("status=" + std::to_string(static_cast<int>(r.m_status)) + " msg=[" + r.m_msg + "]");
                    }
                }
            }
        };

        std::vector<std::thread> posters;
        for (int t = 0; t < 4; ++t)
        {
            posters.emplace_back(apiPoster);
        }
        std::thread pollThread(poller);
        for (auto &th : posters)
        {
            th.join();
        }
        stopPoll.store(true);
        pollThread.join();

        CHECK(apiPostOk.load() == 100, "S5 并发 POST 全部 eOk（100/100）");
        CHECK(pollCount.load() >= 50 && pollOk.load() == pollCount.load(),
              "S5 并发轮询 INFO_LOOP 全部 eOk 且 running:on（polls=" + std::to_string(pollCount.load()) +
                  " ok=" + std::to_string(pollOk.load()) + " 首个失败样本: " +
                  (g_pollFailSamples.empty() ? std::string("无") : g_pollFailSamples.front()) + "）");
        CHECK(waitUntil([]
                        { return g_execCount.load() == 100; },
                        5000),
              "S5 API 层事件全部执行（100/100）");

        CHECK(YOMK_EVENTLOOP_DESTROY("conc_loop").m_status == YomkResponse::eOk, "S5 DESTROY conc_loop 返回 eOk");
        YOMK_SHUTDOWN();
    }

    if (g_failed == 0)
    {
        std::cout << "TestYomkEventLoopConcurrency all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkEventLoopConcurrency FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
