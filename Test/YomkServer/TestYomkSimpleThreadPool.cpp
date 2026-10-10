/**
 * @file TestYomkSimpleThreadPool.cpp
 * @brief YomkSimpleThreadPool 白盒单元测试（正式全面测试，MC1）
 *
 * 覆盖内容：
 * 1. 构造：threadCount=0 默认（硬件并发数一半向上取整兜底 2）/ =1 / =4 均可执行任务
 * 2. post：未停止时返回 true 且任务全部执行；停止后返回 false（拒新）
 * 3. stop：排空语义（存量任务全部执行完毕才返回）；幂等（重复调用不崩溃）
 * 4. 析构兜底：未显式 stop 直接析构，兜底 join 不挂起
 * 5. 异常捕获：任务抛 std::exception / 非 std::exception 均被捕获记日志，进程不终止、后续任务继续执行
 * 6. 并发：多线程并发 post 与 stop 并发，接受数与执行数一致、无崩溃
 * 7. 大量任务：10 万级入队与执行无崩溃
 * 8. 高频 churn 探针（防线三）：stop 时序丢失唤醒放大器——纯空闲 churn（撞窗形态本体：worker 出生即挂起
 *    的 0 任务析构/显式停，线程数矩阵 {1,2,4}）、单任务守恒 churn（挂起/唤醒临界形态 + 析构排空）、
 *    忙时停与并发投递交错 churn（post/stop 持锁互斥回归探针）；规模经 YOMK_TEST_STRESS_SCALE
 *    环境变量参数化（缺省 10 万，run_tests.sh 快速模式 10000，clamp 下限 1000）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "YomkSimpleThreadPool.h"

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

// 辅助：向未停止的池投递 taskCount 个自增任务后 stop，返回实际执行数（排空后统计）
static std::size_t runBatchAndDrain(YomkSimpleThreadPool& pool, std::size_t taskCount)
{
    std::atomic<std::size_t> counter{0};
    for (std::size_t i = 0; i < taskCount; ++i)
    {
        pool.post([&counter]() { counter.fetch_add(1); });
    }
    pool.stop();
    return counter.load();
}

// 1. 构造：三种线程数配置均可正常执行任务
static void testConstruction()
{
    {
        YomkSimpleThreadPool pool;  // threadCount=0 走默认值路径
        CHECK(runBatchAndDrain(pool, 32) == 32, "threadCount=0 默认构造，任务全部执行");
    }
    {
        YomkSimpleThreadPool pool(1);
        CHECK(runBatchAndDrain(pool, 32) == 32, "threadCount=1 构造，任务全部执行");
    }
    {
        YomkSimpleThreadPool pool(4);
        CHECK(runBatchAndDrain(pool, 32) == 32, "threadCount=4 构造，任务全部执行");
    }
}

// 2. post 返回值与排空语义：未停止时返回 true；stop 返回后存量任务已全部执行
static void testPostAndDrain()
{
    YomkSimpleThreadPool pool(2);
    std::atomic<std::size_t> counter{0};
    const std::size_t taskCount = 1000;
    bool allAccepted = true;
    for (std::size_t i = 0; i < taskCount; ++i)
    {
        if (!pool.post([&counter]() { counter.fetch_add(1); }))
        {
            allAccepted = false;
        }
    }
    CHECK(allAccepted, "未停止时 post 恒返回 true");
    pool.stop();
    CHECK(counter.load() == taskCount, "stop 返回后存量任务全部执行完毕（排空语义）");
}

// 3. 拒新语义：停止后 post 返回 false
static void testPostRejectedAfterStop()
{
    YomkSimpleThreadPool pool(1);
    pool.stop();
    CHECK(pool.post([]() {}) == false, "stop 后 post 返回 false（拒新）");
}

// 4. stop 幂等：重复调用直接返回，不影响排空结果
static void testStopIdempotent()
{
    YomkSimpleThreadPool pool(2);
    std::atomic<std::size_t> counter{0};
    for (int i = 0; i < 10; ++i)
    {
        pool.post([&counter]() { counter.fetch_add(1); });
    }
    pool.stop();
    pool.stop();  // 第二次调用应直接返回，不二次 join
    CHECK(counter.load() == 10, "重复 stop 幂等，排空结果不受影响");
}

// 5. 析构兜底：未显式 stop，析构内兜底排空并 join，不挂起
static void testDestructorWithoutStop()
{
    std::atomic<std::size_t> counter{0};
    {
        YomkSimpleThreadPool pool(2);
        for (int i = 0; i < 50; ++i)
        {
            pool.post([&counter]() { counter.fetch_add(1); });
        }
        // 不调用 stop，交由析构兜底
    }
    CHECK(counter.load() == 50, "未显式 stop 时析构兜底排空完成，未挂起");
}

// 6. 异常捕获：任务抛异常被捕获记日志，进程不终止，后续任务继续执行
static void testExceptionCaught()
{
    YomkSimpleThreadPool pool(1);  // 单线程保证执行顺序
    std::atomic<bool> afterRan{false};
    pool.post([]() { throw std::runtime_error("std::exception test"); });
    pool.post([]() { throw 42; });  // 非 std::exception 异常
    pool.post([&afterRan]() { afterRan.store(true); });
    pool.stop();
    CHECK(afterRan.load(), "任务抛异常被捕获，进程不终止且后续任务继续执行");
}

// 7. 并发：多线程并发 post 与 stop 并发，接受数与执行数一致、无崩溃
// 说明：stop 置位后 post 必然返回 false（同一把锁下检查），已接受任务必然被排空执行，
// 故最终 执行数 == 接受数
static void testConcurrentPostWithStop()
{
    YomkSimpleThreadPool pool(4);
    std::atomic<std::size_t> accepted{0};
    std::atomic<std::size_t> executed{0};
    std::vector<std::thread> posters;
    for (int t = 0; t < 8; ++t)
    {
        posters.emplace_back(
            [&pool, &accepted, &executed]()
            {
                for (int i = 0; i < 500; ++i)
                {
                    if (pool.post([&executed]() { executed.fetch_add(1); }))
                    {
                        accepted.fetch_add(1);
                    }
                }
            });
    }
    // 让投递与停止并发交叠
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    pool.stop();
    for (auto& t : posters)
    {
        t.join();
    }
    CHECK(executed.load() == accepted.load(), "并发 post 与 stop：接受任务数等于执行任务数");
}

// 8. 大量任务：10 万级入队与执行无崩溃
static void testLargeVolume()
{
    YomkSimpleThreadPool pool(4);
    std::atomic<std::size_t> counter{0};
    const std::size_t taskCount = 100000;
    bool allAccepted = true;
    for (std::size_t i = 0; i < taskCount; ++i)
    {
        if (!pool.post([&counter]() { counter.fetch_add(1); }))
        {
            allAccepted = false;
            break;
        }
    }
    CHECK(allAccepted, "10 万级任务入队全部成功");
    pool.stop();
    CHECK(counter.load() == taskCount, "10 万级任务全部执行完毕");
}

// 压力规模：环境变量 YOMK_TEST_STRESS_SCALE（run_tests.sh 快速模式 10000；缺省 10 万），clamp 下限 1000
// （对齐 TestYomkEventLoopStress::stressScale 同款惯例）
static std::size_t stressScale()
{
    const char* env = std::getenv("YOMK_TEST_STRESS_SCALE");
    if (env == nullptr || *env == '\0')
    {
        return 100000;
    }
    unsigned long v = std::strtoul(env, nullptr, 10);
    return v < 1000 ? 1000 : static_cast<std::size_t>(v);
}

// 9. 高频 churn 探针：stop 时序丢失唤醒放大器（防线三）
// 背景：stop() 曾存在"锁外置位 + 锁外 notify"与 worker 持锁谓词检查间的纳秒级丢失唤醒窗口
// （run_tests.sh 偶发 TestYomkServer 300s 超时，修复对齐 EventLoop::stop 先例，见 stop() 注释）。
// 该竞态属逻辑竞态：TSan 静默、行覆盖 100% 依旧漏检，唯一低成本检出手段是高频 churn 堆积
// 窗口命中机会。若回归，本用例表现为进程挂死（run_tests.sh 单测试超时兜底报 FAIL；
// 手动排查时用 stdbuf -oL 实时观察卡点）。
static void testStopTimingChurn()
{
    const std::size_t n = stressScale();

    // N1 纯空闲 churn（撞窗形态本体）：worker 出生即挂起，0 任务下显式停/析构兜底双路径；
    // 线程数矩阵 {1,2,4} 覆盖不同唤醒扇出。无可断言状态——跑到节末即探针通过，挂死即失败
    {
        const std::size_t perCfg = n / 3 + 1;
        const std::size_t threadCfgs[] = {1, 2, 4};
        for (std::size_t cfg = 0; cfg < 3; ++cfg)
        {
            for (std::size_t i = 0; i < perCfg; ++i)
            {
                YomkSimpleThreadPool poolExplicit(threadCfgs[cfg]);
                poolExplicit.stop();  // 显式停：命中"挂起 worker 收 notify"路径
            }
            for (std::size_t i = 0; i < perCfg; ++i)
            {
                YomkSimpleThreadPool poolDtor(threadCfgs[cfg]);
                // 不显式 stop：命中析构兜底 stop 路径（TestYomkServer 撞窗形态）
            }
        }
        CHECK(perCfg > 0, "N1 纯空闲 churn 完成（3 线程数 x 显式停/析构兜底 x " + std::to_string(perCfg) + " 轮，无挂死）");
    }

    // N3 单任务守恒 churn（主断言载体）：worker 在"挂起收任务唤醒执行"的临界形态 + 析构排空，
    // 总执行数必须等于轮数（每轮 1 任务，排空语义保证执行；丢唤醒或丢任务都会在这里暴露）
    {
        std::atomic<std::size_t> executed{0};
        for (std::size_t i = 0; i < n; ++i)
        {
            YomkSimpleThreadPool pool(1);
            pool.post([&executed]() { executed.fetch_add(1); });
        }
        CHECK(executed.load() == n, "N3 单任务 churn x " + std::to_string(n) + " 轮全部执行（守恒）");
    }

    // N4 忙时停与并发投递交错 churn：post 洪流中 stop（worker 忙态 + poster 竞争路径，
    // post/stop 持锁互斥回归探针），每轮 接受数==执行数（拒新 + 排空，异常计入失败计数节末统一断言）
    {
        const std::size_t rounds = n / 50 + 100;
        std::size_t churnFail = 0;
        for (std::size_t r = 0; r < rounds; ++r)
        {
            YomkSimpleThreadPool pool(4);
            std::atomic<std::size_t> accepted{0};
            std::atomic<std::size_t> executed{0};
            std::vector<std::thread> posters;
            for (int t = 0; t < 2; ++t)
            {
                posters.emplace_back(
                    [&pool, &accepted, &executed]()
                    {
                        for (int i = 0; i < 500; ++i)
                        {
                            if (pool.post([&executed]() { executed.fetch_add(1); }))
                            {
                                accepted.fetch_add(1);
                            }
                        }
                    });
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            pool.stop();  // 主线程忙时停：与 poster 并发
            for (auto& t : posters)
            {
                t.join();
            }
            if (executed.load() != accepted.load())
            {
                ++churnFail;
            }
        }
        CHECK(churnFail == 0, "N4 忙时停交错 churn x " + std::to_string(rounds) + " 轮全部守恒（接受数==执行数）");
    }
}

int main()
{
    testConstruction();
    testPostAndDrain();
    testPostRejectedAfterStop();
    testStopIdempotent();
    testDestructorWithoutStop();
    testExceptionCaught();
    testConcurrentPostWithStop();
    testLargeVolume();
    testStopTimingChurn();

    if (g_failed > 0)
    {
        std::cout << "TestYomkSimpleThreadPool failed, count: " << g_failed << std::endl;
        return 1;
    }
    std::cout << "TestYomkSimpleThreadPool all check passed." << std::endl;
    return 0;
}
