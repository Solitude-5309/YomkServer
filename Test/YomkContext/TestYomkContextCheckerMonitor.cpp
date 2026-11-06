/**
 * @file TestYomkContextCheckerMonitor.cpp
 * @brief YomkContext checker 与 monitor（同步/异步）白盒测试（Context 模块 MC2）
 *
 * 覆盖内容：
 * 1. set_checker 边界：空 key、null checkFunc、不存在 key、正常设置
 * 2. turn_on_checker/turn_off_checker 开关语义
 * 3. checker accept/reject：accept 放行、reject 拦截且 value 不变
 * 4. 空 checker 守卫（对应前置修复）：全局开启 checker 后对未设 checker 的 key set 视为 accept，不崩溃
 * 5. checker 关闭后不生效
 * 6. set_monitor 边界：空 key、null func、不存在 key、正常设置
 * 7. turn_on_monitor/turn_off_monitor 开关语义
 * 8. 同步 monitor 触发（拿到最新数据）、关闭后不触发
 * 9. 同步 monitor 异常吞掉（std::exception 与非 std::exception）
 * 10. 多同步 monitor 按注册顺序触发
 * 11. 异步 monitor 触发与顺序（单线程池保序）
 * 12. 并发 set 异步保序：锁内入队 ⇒ 末条异步通知收敛到最终值（C1 回归）
 * 13. 异步 monitor 排空（deinit）：shutdown 前排空全部异步任务
 *
 * 说明：全程使用 yomk::server().init() 单例拉起内置 /YomkContext；异步排空用例置于末尾并调用 yomk::server().shutdown()
 *       （call_once 约束，shutdown 后不可再初始化）；服务删除后重新注册/池重建归 MC3
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "YomkApi.h"

using yomk::payload::CheckStatus;

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

// ---- 文件级观测变量 ----
static std::atomic<int> g_checkerCalls{0};      // checker 被调用次数
static std::atomic<int> g_syncMonitorCalls{0};  // 同步 monitor 被调用次数
static std::atomic<int> g_throwACalls{0};       // 抛 std::exception 的 monitor 调用次数
static std::atomic<int> g_throwBCalls{0};       // 抛非 std::exception 的 monitor 调用次数
static std::atomic<int> g_asyncCalls{0};        // 异步 monitor（顺序用例）调用次数
static std::atomic<int> g_drainCalls{0};        // 异步 monitor（排空用例）调用次数

static std::string g_lastSyncValue;            // 最近一次同步 monitor 看到的 value
static std::vector<std::string> g_order;       // 多同步 monitor 触发顺序
static std::mutex g_asyncMutex;                // 保护异步顺序记录
static std::vector<std::string> g_asyncOrder;  // 异步 monitor 到达顺序

static void resetGlobals()
{
    g_checkerCalls.store(0);
    g_syncMonitorCalls.store(0);
    g_throwACalls.store(0);
    g_throwBCalls.store(0);
    g_asyncCalls.store(0);
    g_drainCalls.store(0);
    g_lastSyncValue.clear();
    g_order.clear();
    {
        std::lock_guard<std::mutex> lk(g_asyncMutex);
        g_asyncOrder.clear();
    }
}

// 从 Context 数据中取出 String value（失败返回空串）
static std::string contextStringValue(const yomk::payload::Context& ctx)
{
    auto sp = std::dynamic_pointer_cast<yomk::msgs::String>(ctx.m_value);
    return sp ? sp->d : std::string();
}

// 轮询等待原子计数达到 target，超时返回是否达标
static bool waitForCount(std::atomic<int>& counter, int target, int timeoutMs)
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (counter.load() >= target)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return counter.load() >= target;
}

int main()
{
    resetGlobals();

    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

    // ============ Section 1: set_checker 边界 ============
    {
        auto cr = yomk::ctx().key("ck_key").value(yomk::msgs::String::shared(std::string("init"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 ck_key 成功");

        auto emptyKey = yomk::ctx().key("").checker_fn([](const yomk::payload::Context&) { return CheckStatus::Accept; }).apply();
        YOMK_TEST_CHECK(emptyKey == yomk::payload::ResStatus::Failed, "set_checker 空 key 返回 ResStatus::Failed");

        auto nullFunc = yomk::ctx().key("ck_key").checker_fn(nullptr).apply();
        YOMK_TEST_CHECK(nullFunc == yomk::payload::ResStatus::Failed, "set_checker null checkFunc 返回 ResStatus::Failed");

        auto noKey =
            yomk::ctx().key("no_such_key").checker_fn([](const yomk::payload::Context&) { return CheckStatus::Accept; }).apply();
        YOMK_TEST_CHECK(noKey == yomk::payload::ResStatus::Failed, "set_checker 不存在 key 返回 ResStatus::Failed");

        auto ok = yomk::ctx().key("ck_key").checker_fn([](const yomk::payload::Context&) { return CheckStatus::Accept; }).apply();
        YOMK_TEST_CHECK(ok == yomk::payload::ResStatus::Succeeded, "set_checker 正常设置返回 ResStatus::Succeeded");
    }

    // ============ Section 2: checker 开关 ============
    {
        auto on = yomk::ctx().group().checker().enable();
        YOMK_TEST_CHECK(on == yomk::payload::ResStatus::Succeeded, "turn_on_checker 返回 ResStatus::Succeeded");

        auto off = yomk::ctx().group().checker().disable();
        YOMK_TEST_CHECK(off == yomk::payload::ResStatus::Succeeded, "turn_off_checker 返回 ResStatus::Succeeded");
    }

    // ============ Section 3: checker accept/reject ============
    {
        // accept-checker，全局开启
        yomk::ctx().key("ck_key").checker_fn([](const yomk::payload::Context&)
            {
                ++g_checkerCalls;
                return CheckStatus::Accept;
            }).apply();
        yomk::ctx().group().checker().enable();

        g_checkerCalls.store(0);
        auto acceptSet = yomk::ctx().key("ck_key").value(yomk::msgs::String::shared(std::string("accepted"))).set();
        YOMK_TEST_CHECK(acceptSet == yomk::payload::ResStatus::Succeeded, "accept-checker 下 set 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(g_checkerCalls.load() == 1, "accept-checker 被调用 1 次");

        // reject-checker 覆盖
        yomk::ctx().key("ck_key").checker_fn([](const yomk::payload::Context&)
            {
                ++g_checkerCalls;
                return CheckStatus::Reject;
            }).apply();
        g_checkerCalls.store(0);
        auto rejectSet = yomk::ctx().key("ck_key").value(yomk::msgs::String::shared(std::string("rejected"))).set();
        YOMK_TEST_CHECK(rejectSet == yomk::payload::ResStatus::Failed, "reject-checker 下 set 返回 ResStatus::Failed");
        YOMK_TEST_CHECK(g_checkerCalls.load() == 1, "reject-checker 被调用 1 次");

        // value 未变（仍为 accepted）
        auto got = yomk::ctx().key("ck_key").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string("default"))).get();
        YOMK_TEST_CHECK(got && got->d == "accepted", "reject 后 value 保持不变");
    }

    // ============ Section 4: 空 checker 守卫（对应前置修复）============
    {
        // checker 仍全局开启；ck_nochecker 从未设置 checker
        auto cr = yomk::ctx().key("ck_nochecker").value(yomk::msgs::String::shared(std::string("v0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 ck_nochecker 成功");

        auto resp = yomk::ctx().key("ck_nochecker").value(yomk::msgs::String::shared(std::string("v1"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "全局开启 checker 但未设 checker 的 key set 视为 accept（不崩溃）");

        auto got = yomk::ctx().key("ck_nochecker").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string("default"))).get();
        YOMK_TEST_CHECK(got && got->d == "v1", "空 checker 守卫下 value 正常更新");
    }

    // ============ Section 5: checker 关闭后不生效 ============
    {
        yomk::ctx().group().checker().disable();
        // ck_key 当前为 reject-checker，但 checker 已关闭，set 应成功
        auto resp = yomk::ctx().key("ck_key").value(yomk::msgs::String::shared(std::string("after_off"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "checker 关闭后 reject-checker 不生效，set 成功");

        auto got = yomk::ctx().key("ck_key").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string("default"))).get();
        YOMK_TEST_CHECK(got && got->d == "after_off", "checker 关闭后 value 更新成功");
    }

    // ============ Section 6: set_monitor 边界 ============
    {
        auto cr = yomk::ctx().key("mon_bound").value(yomk::msgs::String::shared(std::string("m0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_bound 成功");

        auto emptyKey = yomk::ctx().key("").monitor_fn([](const yomk::payload::Context&) {}).apply();
        YOMK_TEST_CHECK(emptyKey == yomk::payload::ResStatus::Failed, "set_monitor 空 key 返回 ResStatus::Failed");

        auto nullFunc = yomk::ctx().key("mon_bound").monitor_fn(nullptr).apply();
        YOMK_TEST_CHECK(nullFunc == yomk::payload::ResStatus::Failed, "set_monitor null func 返回 ResStatus::Failed");

        auto noKey = yomk::ctx().key("no_such_key").monitor_fn([](const yomk::payload::Context&) {}).apply();
        YOMK_TEST_CHECK(noKey == yomk::payload::ResStatus::Failed, "set_monitor 不存在 key 返回 ResStatus::Failed");

        auto ok = yomk::ctx().key("mon_bound").monitor_fn([](const yomk::payload::Context&) {}).apply();
        YOMK_TEST_CHECK(ok == yomk::payload::ResStatus::Succeeded, "set_monitor 正常设置返回 ResStatus::Succeeded");
    }

    // ============ Section 7: monitor 开关 ============
    {
        auto on = yomk::ctx().group().monitor().enable();
        YOMK_TEST_CHECK(on == yomk::payload::ResStatus::Succeeded, "turn_on_monitor 返回 ResStatus::Succeeded");

        auto off = yomk::ctx().group().monitor().disable();
        YOMK_TEST_CHECK(off == yomk::payload::ResStatus::Succeeded, "turn_off_monitor 返回 ResStatus::Succeeded");
    }

    // ============ Section 8: 同步 monitor 触发 ============
    {
        auto cr = yomk::ctx().key("mon_sync").value(yomk::msgs::String::shared(std::string("s0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_sync 成功");

        yomk::ctx().key("mon_sync").monitor_fn([](const yomk::payload::Context& ctx)
            {
                ++g_syncMonitorCalls;
                g_lastSyncValue = contextStringValue(ctx);
            }).apply();

        yomk::ctx().group().monitor().enable();
        g_syncMonitorCalls.store(0);
        g_lastSyncValue.clear();

        auto resp = yomk::ctx().key("mon_sync").value(yomk::msgs::String::shared(std::string("s1"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "开启 monitor 后 set 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(g_syncMonitorCalls.load() == 1, "同步 monitor 被调用 1 次");
        YOMK_TEST_CHECK(g_lastSyncValue == "s1", "同步 monitor 拿到最新 value s1");

        yomk::ctx().group().monitor().disable();
        g_syncMonitorCalls.store(0);
        auto resp2 = yomk::ctx().key("mon_sync").value(yomk::msgs::String::shared(std::string("s2"))).set();
        YOMK_TEST_CHECK(resp2 == yomk::payload::ResStatus::Succeeded, "关闭 monitor 后 set 仍返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(g_syncMonitorCalls.load() == 0, "关闭 monitor 后同步 monitor 不触发");
    }

    // ============ Section 9: 同步 monitor 异常吞掉 ============
    {
        yomk::ctx().group().monitor().enable();
        auto cr = yomk::ctx().key("mon_throw").value(yomk::msgs::String::shared(std::string("t0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_throw 成功");

        // monitor A：抛 std::exception
        yomk::ctx().key("mon_throw").monitor_fn([](const yomk::payload::Context&)
            {
                ++g_throwACalls;
                throw std::runtime_error("sync monitor std exception");
            }).apply();
        // monitor B：抛非 std::exception
        yomk::ctx().key("mon_throw").monitor_fn([](const yomk::payload::Context&)
            {
                ++g_throwBCalls;
                throw 42;
            }).apply();
        // monitor C：正常，验证异常不影响后续 monitor
        yomk::ctx().key("mon_throw").monitor_fn([](const yomk::payload::Context&) { ++g_syncMonitorCalls; }).apply();

        g_throwACalls.store(0);
        g_throwBCalls.store(0);
        g_syncMonitorCalls.store(0);

        auto resp = yomk::ctx().key("mon_throw").value(yomk::msgs::String::shared(std::string("t1"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "同步 monitor 抛异常被吞掉，set 仍返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(g_throwACalls.load() == 1, "抛 std::exception 的 monitor 被调用");
        YOMK_TEST_CHECK(g_throwBCalls.load() == 1, "抛非 std::exception 的 monitor 被调用");
        YOMK_TEST_CHECK(g_syncMonitorCalls.load() == 1, "异常 monitor 之后的正常 monitor 继续执行");
        yomk::ctx().group().monitor().disable();
    }

    // ============ Section 10: 多同步 monitor 顺序 ============
    {
        yomk::ctx().group().monitor().enable();
        auto cr = yomk::ctx().key("mon_multi").value(yomk::msgs::String::shared(std::string("u0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_multi 成功");

        g_order.clear();
        yomk::ctx().key("mon_multi").monitor_fn([](const yomk::payload::Context&) { g_order.push_back("1"); }).apply();
        yomk::ctx().key("mon_multi").monitor_fn([](const yomk::payload::Context&) { g_order.push_back("2"); }).apply();
        yomk::ctx().key("mon_multi").monitor_fn([](const yomk::payload::Context&) { g_order.push_back("3"); }).apply();

        auto resp = yomk::ctx().key("mon_multi").value(yomk::msgs::String::shared(std::string("u1"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "多 monitor set 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(g_order.size() == 3, "3 个同步 monitor 全部触发");
        YOMK_TEST_CHECK(
            g_order.size() == 3 && g_order[0] == "1" && g_order[1] == "2" && g_order[2] == "3",
            "多同步 monitor 按注册顺序触发");
        yomk::ctx().group().monitor().disable();
    }

    // ============ Section 11: 异步 monitor 触发与顺序 ============
    {
        yomk::ctx().group().monitor().enable();
        auto cr = yomk::ctx().key("mon_async").value(yomk::msgs::String::shared(std::string("a0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_async 成功");

        g_asyncCalls.store(0);
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            g_asyncOrder.clear();
        }
        yomk::ctx().key("mon_async").monitor_fn([](const yomk::payload::Context& ctx)
            {
                std::string v = contextStringValue(ctx);
                {
                    std::lock_guard<std::mutex> lk(g_asyncMutex);
                    g_asyncOrder.push_back(v);
                }
                ++g_asyncCalls;
            }).async(/*async=*/true).apply();

        yomk::ctx().key("mon_async").value(yomk::msgs::String::shared(std::string("a1"))).set();
        yomk::ctx().key("mon_async").value(yomk::msgs::String::shared(std::string("a2"))).set();
        yomk::ctx().key("mon_async").value(yomk::msgs::String::shared(std::string("a3"))).set();

        bool reached = waitForCount(g_asyncCalls, 3, 2000);
        YOMK_TEST_CHECK(reached, "异步 monitor 在等待窗口内全部触发（计数 == 3）");
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            YOMK_TEST_CHECK(
                g_asyncOrder.size() == 3 && g_asyncOrder[0] == "a1" && g_asyncOrder[1] == "a2" &&
                    g_asyncOrder[2] == "a3",
                "异步 monitor 按 set 顺序到达（单线程池保序）");
        }
        yomk::ctx().group().monitor().disable();
    }

    // ============ Section 12: 并发 set 异步保序（末条收敛真值，C1 回归）============
    {
        yomk::ctx().group().monitor().enable();
        auto cr = yomk::ctx().key("mon_conc").value(yomk::msgs::String::shared(std::string("c_init"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_conc 成功");

        g_asyncCalls.store(0);
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            g_asyncOrder.clear();
        }
        yomk::ctx().key("mon_conc").monitor_fn([](const yomk::payload::Context& ctx)
            {
                std::string v = contextStringValue(ctx);
                {
                    std::lock_guard<std::mutex> lk(g_asyncMutex);
                    g_asyncOrder.push_back(v);
                }
                ++g_asyncCalls;
            }).async(/*async=*/true).apply();

        // N 个线程并发 set（各值可辨识）；C1 下异步入队在写锁内完成 ⇒ 入队序=提交序，
        // 单线程 FIFO 池 ⇒ 末条异步通知 = 最后一次提交的值 = 最终 getContext 值。
        // 旧实现（post 在解锁后）可能倒挂：先提交者后入队 ⇒ 末条 ≠ 终值。
        const int kThreads = 8;
        const int kIters = 20;
        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            workers.emplace_back(
                [t, kIters]()
                {
                    for (int i = 0; i < kIters; ++i)
                    {
                        std::string v = "t" + std::to_string(t) + "_i" + std::to_string(i);
                        yomk::ctx().key("mon_conc").value(yomk::msgs::String::shared(v)).set();
                    }
                });
        }
        for (auto& w : workers)
        {
            w.join();
        }

        const int kTotal = kThreads * kIters;
        bool reached = waitForCount(g_asyncCalls, kTotal, 15000);
        YOMK_TEST_CHECK(reached, "并发 set 后异步 monitor 在等待窗口内全部触发（计数 == 总提交数）");

        // 所有写线程已 join，getContext 终值稳定
        auto finalVal = yomk::ctx().key("mon_conc").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string("default"))).get();
        YOMK_TEST_CHECK(finalVal && !finalVal->d.empty(), "读取 mon_conc 终值成功");

        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            YOMK_TEST_CHECK(g_asyncOrder.size() == static_cast<size_t>(kTotal), "异步顺序记录条数 == 总提交数");
            YOMK_TEST_CHECK(
                !g_asyncOrder.empty() && finalVal && g_asyncOrder.back() == finalVal->d,
                "并发 set 下异步末条 == 最终值（锁内入队 ⇒ 末条收敛真值）");
        }
        yomk::ctx().group().monitor().disable();
    }

    // ============ Section 13: 异步 monitor 排空（deinit），末尾调用 shutdown ============
    {
        yomk::ctx().group().monitor().enable();
        auto cr = yomk::ctx().key("mon_drain").value(yomk::msgs::String::shared(std::string("d0"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mon_drain 成功");

        g_drainCalls.store(0);
        yomk::ctx().key("mon_drain").monitor_fn([](const yomk::payload::Context&) { ++g_drainCalls; }).async(/*async=*/true).apply();

        const int N = 50;
        for (int i = 0; i < N; ++i)
        {
            yomk::ctx().key("mon_drain").value(yomk::msgs::String::shared(std::string("d") + std::to_string(i))).set();
        }

        // 立即 shutdown，触发 Context deinit 排空 monitor 池
        yomk::server().shutdown();

        // shutdown 返回后排空已完成，计数应等于投递次数
        YOMK_TEST_CHECK(g_drainCalls.load() == N, "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到、无丢失）");
    }

    if (g_failed == 0)
    {
        std::cout << "TestYomkContextCheckerMonitor all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkContextCheckerMonitor FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
