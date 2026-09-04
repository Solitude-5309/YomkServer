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
 * 说明：全程使用 YOMK_INIT 单例拉起内置 /YomkContext；异步排空用例置于末尾并调用 YOMK_SHUTDOWN
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

// ---- 文件级观测变量 ----
static std::atomic<int> g_checkerCalls{0};     // checker 被调用次数
static std::atomic<int> g_syncMonitorCalls{0}; // 同步 monitor 被调用次数
static std::atomic<int> g_throwACalls{0};      // 抛 std::exception 的 monitor 调用次数
static std::atomic<int> g_throwBCalls{0};      // 抛非 std::exception 的 monitor 调用次数
static std::atomic<int> g_asyncCalls{0};       // 异步 monitor（顺序用例）调用次数
static std::atomic<int> g_drainCalls{0};       // 异步 monitor（排空用例）调用次数

static std::string g_lastSyncValue;           // 最近一次同步 monitor 看到的 value
static std::vector<std::string> g_order;      // 多同步 monitor 触发顺序
static std::mutex g_asyncMutex;               // 保护异步顺序记录
static std::vector<std::string> g_asyncOrder; // 异步 monitor 到达顺序

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
static std::string contextStringValue(const yomk::Context &ctx)
{
    auto sp = std::dynamic_pointer_cast<Yomk(String)>(ctx.m_value);
    return sp ? sp->d : std::string();
}

// 轮询等待原子计数达到 target，超时返回是否达标
static bool waitForCount(std::atomic<int> &counter, int target, int timeoutMs)
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

    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    // ============ Section 1: set_checker 边界 ============
    {
        auto cr = YOMK_CONTEXT_CREATE("ck_key", YomkMkPtr(String, std::string("init")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 ck_key 成功");

        auto emptyKey = YOMK_CONTEXT_SET_CHECKER("", [](const yomk::Context &)
                                                 { return ContextChecker::eAccept; });
        CHECK(emptyKey.m_status == YomkResponse::eNo, "set_checker 空 key 返回 eNo");
        CHECK(emptyKey.m_msg == "key is empty", "set_checker 空 key 消息一致");

        auto nullFunc = YOMK_CONTEXT_SET_CHECKER("ck_key", nullptr);
        CHECK(nullFunc.m_status == YomkResponse::eNo, "set_checker null checkFunc 返回 eNo");
        CHECK(nullFunc.m_msg == "checkFunc is empty", "set_checker null checkFunc 消息一致");

        auto noKey = YOMK_CONTEXT_SET_CHECKER("no_such_key", [](const yomk::Context &)
                                              { return ContextChecker::eAccept; });
        CHECK(noKey.m_status == YomkResponse::eNo, "set_checker 不存在 key 返回 eNo");
        CHECK(noKey.m_msg == "key is not exist", "set_checker 不存在 key 消息一致");

        auto ok = YOMK_CONTEXT_SET_CHECKER("ck_key", [](const yomk::Context &)
                                           { return ContextChecker::eAccept; });
        CHECK(ok.m_status == YomkResponse::eOk, "set_checker 正常设置返回 eOk");
        CHECK(ok.m_msg == "set checker success", "set_checker 成功消息一致");
    }

    // ============ Section 2: checker 开关 ============
    {
        auto on = YOMK_CONTEXT_ON_CHECKER();
        CHECK(on.m_status == YomkResponse::eOk, "turn_on_checker 返回 eOk");
        CHECK(on.m_msg == "turn on checker success", "turn_on_checker 消息一致");

        auto off = YOMK_CONTEXT_OFF_CHECKER();
        CHECK(off.m_status == YomkResponse::eOk, "turn_off_checker 返回 eOk");
        CHECK(off.m_msg == "turn off checker success", "turn_off_checker 消息一致");
    }

    // ============ Section 3: checker accept/reject ============
    {
        // accept-checker，全局开启
        YOMK_CONTEXT_SET_CHECKER("ck_key", [](const yomk::Context &)
                                 {
            ++g_checkerCalls;
            return ContextChecker::eAccept; });
        YOMK_CONTEXT_ON_CHECKER();

        g_checkerCalls.store(0);
        auto acceptSet = YOMK_CONTEXT_SET("ck_key", YomkMkPtr(String, std::string("accepted")));
        CHECK(acceptSet.m_status == YomkResponse::eOk, "accept-checker 下 set 返回 eOk");
        CHECK(g_checkerCalls.load() == 1, "accept-checker 被调用 1 次");

        // reject-checker 覆盖
        YOMK_CONTEXT_SET_CHECKER("ck_key", [](const yomk::Context &)
                                 {
            ++g_checkerCalls;
            return ContextChecker::eReject; });
        g_checkerCalls.store(0);
        auto rejectSet = YOMK_CONTEXT_SET("ck_key", YomkMkPtr(String, std::string("rejected")));
        CHECK(rejectSet.m_status == YomkResponse::eNo, "reject-checker 下 set 返回 eNo");
        CHECK(rejectSet.m_msg == "checker reject set context", "reject-checker 消息一致");
        CHECK(g_checkerCalls.load() == 1, "reject-checker 被调用 1 次");

        // value 未变（仍为 accepted）
        auto got = YOMK_CONTEXT_GET(String, "ck_key", YomkMkPtr(String, std::string("default")));
        CHECK(got && got->d == "accepted", "reject 后 value 保持不变");
    }

    // ============ Section 4: 空 checker 守卫（对应前置修复）============
    {
        // checker 仍全局开启；ck_nochecker 从未设置 checker
        auto cr = YOMK_CONTEXT_CREATE("ck_nochecker", YomkMkPtr(String, std::string("v0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 ck_nochecker 成功");

        auto resp = YOMK_CONTEXT_SET("ck_nochecker", YomkMkPtr(String, std::string("v1")));
        CHECK(resp.m_status == YomkResponse::eOk, "全局开启 checker 但未设 checker 的 key set 视为 accept（不崩溃）");

        auto got = YOMK_CONTEXT_GET(String, "ck_nochecker", YomkMkPtr(String, std::string("default")));
        CHECK(got && got->d == "v1", "空 checker 守卫下 value 正常更新");
    }

    // ============ Section 5: checker 关闭后不生效 ============
    {
        YOMK_CONTEXT_OFF_CHECKER();
        // ck_key 当前为 reject-checker，但 checker 已关闭，set 应成功
        auto resp = YOMK_CONTEXT_SET("ck_key", YomkMkPtr(String, std::string("after_off")));
        CHECK(resp.m_status == YomkResponse::eOk, "checker 关闭后 reject-checker 不生效，set 成功");

        auto got = YOMK_CONTEXT_GET(String, "ck_key", YomkMkPtr(String, std::string("default")));
        CHECK(got && got->d == "after_off", "checker 关闭后 value 更新成功");
    }

    // ============ Section 6: set_monitor 边界 ============
    {
        auto cr = YOMK_CONTEXT_CREATE("mon_bound", YomkMkPtr(String, std::string("m0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_bound 成功");

        auto emptyKey = YOMK_CONTEXT_SET_MONITOR("", [](const yomk::Context &) {});
        CHECK(emptyKey.m_status == YomkResponse::eNo, "set_monitor 空 key 返回 eNo");
        CHECK(emptyKey.m_msg == "key is empty", "set_monitor 空 key 消息一致");

        auto nullFunc = YOMK_CONTEXT_SET_MONITOR("mon_bound", nullptr);
        CHECK(nullFunc.m_status == YomkResponse::eNo, "set_monitor null func 返回 eNo");
        CHECK(nullFunc.m_msg == "context monitor function is empty", "set_monitor null func 消息一致");

        auto noKey = YOMK_CONTEXT_SET_MONITOR("no_such_key", [](const yomk::Context &) {});
        CHECK(noKey.m_status == YomkResponse::eNo, "set_monitor 不存在 key 返回 eNo");
        CHECK(noKey.m_msg == "key is not exist", "set_monitor 不存在 key 消息一致");

        auto ok = YOMK_CONTEXT_SET_MONITOR("mon_bound", [](const yomk::Context &) {});
        CHECK(ok.m_status == YomkResponse::eOk, "set_monitor 正常设置返回 eOk");
        CHECK(ok.m_msg == "set context monitor success", "set_monitor 成功消息一致");
    }

    // ============ Section 7: monitor 开关 ============
    {
        auto on = YOMK_CONTEXT_ON_MONITOR();
        CHECK(on.m_status == YomkResponse::eOk, "turn_on_monitor 返回 eOk");
        CHECK(on.m_msg == "turn on monitor success", "turn_on_monitor 消息一致");

        auto off = YOMK_CONTEXT_OFF_MONITOR();
        CHECK(off.m_status == YomkResponse::eOk, "turn_off_monitor 返回 eOk");
        CHECK(off.m_msg == "turn off monitor success", "turn_off_monitor 消息一致");
    }

    // ============ Section 8: 同步 monitor 触发 ============
    {
        auto cr = YOMK_CONTEXT_CREATE("mon_sync", YomkMkPtr(String, std::string("s0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_sync 成功");

        YOMK_CONTEXT_SET_MONITOR("mon_sync", [](const yomk::Context &ctx)
                                 {
            ++g_syncMonitorCalls;
            g_lastSyncValue = contextStringValue(ctx); });

        YOMK_CONTEXT_ON_MONITOR();
        g_syncMonitorCalls.store(0);
        g_lastSyncValue.clear();

        auto resp = YOMK_CONTEXT_SET("mon_sync", YomkMkPtr(String, std::string("s1")));
        CHECK(resp.m_status == YomkResponse::eOk, "开启 monitor 后 set 返回 eOk");
        CHECK(g_syncMonitorCalls.load() == 1, "同步 monitor 被调用 1 次");
        CHECK(g_lastSyncValue == "s1", "同步 monitor 拿到最新 value s1");

        YOMK_CONTEXT_OFF_MONITOR();
        g_syncMonitorCalls.store(0);
        auto resp2 = YOMK_CONTEXT_SET("mon_sync", YomkMkPtr(String, std::string("s2")));
        CHECK(resp2.m_status == YomkResponse::eOk, "关闭 monitor 后 set 仍返回 eOk");
        CHECK(g_syncMonitorCalls.load() == 0, "关闭 monitor 后同步 monitor 不触发");
    }

    // ============ Section 9: 同步 monitor 异常吞掉 ============
    {
        YOMK_CONTEXT_ON_MONITOR();
        auto cr = YOMK_CONTEXT_CREATE("mon_throw", YomkMkPtr(String, std::string("t0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_throw 成功");

        // monitor A：抛 std::exception
        YOMK_CONTEXT_SET_MONITOR("mon_throw", [](const yomk::Context &)
                                 {
            ++g_throwACalls;
            throw std::runtime_error("sync monitor std exception"); });
        // monitor B：抛非 std::exception
        YOMK_CONTEXT_SET_MONITOR("mon_throw", [](const yomk::Context &)
                                 {
            ++g_throwBCalls;
            throw 42; });
        // monitor C：正常，验证异常不影响后续 monitor
        YOMK_CONTEXT_SET_MONITOR("mon_throw", [](const yomk::Context &)
                                 { ++g_syncMonitorCalls; });

        g_throwACalls.store(0);
        g_throwBCalls.store(0);
        g_syncMonitorCalls.store(0);

        auto resp = YOMK_CONTEXT_SET("mon_throw", YomkMkPtr(String, std::string("t1")));
        CHECK(resp.m_status == YomkResponse::eOk, "同步 monitor 抛异常被吞掉，set 仍返回 eOk");
        CHECK(g_throwACalls.load() == 1, "抛 std::exception 的 monitor 被调用");
        CHECK(g_throwBCalls.load() == 1, "抛非 std::exception 的 monitor 被调用");
        CHECK(g_syncMonitorCalls.load() == 1, "异常 monitor 之后的正常 monitor 继续执行");
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 10: 多同步 monitor 顺序 ============
    {
        YOMK_CONTEXT_ON_MONITOR();
        auto cr = YOMK_CONTEXT_CREATE("mon_multi", YomkMkPtr(String, std::string("u0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_multi 成功");

        g_order.clear();
        YOMK_CONTEXT_SET_MONITOR("mon_multi", [](const yomk::Context &)
                                 { g_order.push_back("1"); });
        YOMK_CONTEXT_SET_MONITOR("mon_multi", [](const yomk::Context &)
                                 { g_order.push_back("2"); });
        YOMK_CONTEXT_SET_MONITOR("mon_multi", [](const yomk::Context &)
                                 { g_order.push_back("3"); });

        auto resp = YOMK_CONTEXT_SET("mon_multi", YomkMkPtr(String, std::string("u1")));
        CHECK(resp.m_status == YomkResponse::eOk, "多 monitor set 返回 eOk");
        CHECK(g_order.size() == 3, "3 个同步 monitor 全部触发");
        CHECK(g_order.size() == 3 && g_order[0] == "1" && g_order[1] == "2" && g_order[2] == "3",
              "多同步 monitor 按注册顺序触发");
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 11: 异步 monitor 触发与顺序 ============
    {
        YOMK_CONTEXT_ON_MONITOR();
        auto cr = YOMK_CONTEXT_CREATE("mon_async", YomkMkPtr(String, std::string("a0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_async 成功");

        g_asyncCalls.store(0);
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            g_asyncOrder.clear();
        }
        YOMK_CONTEXT_SET_MONITOR("mon_async", [](const yomk::Context &ctx)
                                 {
            std::string v = contextStringValue(ctx);
            {
                std::lock_guard<std::mutex> lk(g_asyncMutex);
                g_asyncOrder.push_back(v);
            }
            ++g_asyncCalls; }, /*async=*/true);

        YOMK_CONTEXT_SET("mon_async", YomkMkPtr(String, std::string("a1")));
        YOMK_CONTEXT_SET("mon_async", YomkMkPtr(String, std::string("a2")));
        YOMK_CONTEXT_SET("mon_async", YomkMkPtr(String, std::string("a3")));

        bool reached = waitForCount(g_asyncCalls, 3, 2000);
        CHECK(reached, "异步 monitor 在等待窗口内全部触发（计数 == 3）");
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            CHECK(g_asyncOrder.size() == 3 &&
                      g_asyncOrder[0] == "a1" && g_asyncOrder[1] == "a2" && g_asyncOrder[2] == "a3",
                  "异步 monitor 按 set 顺序到达（单线程池保序）");
        }
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 12: 并发 set 异步保序（末条收敛真值，C1 回归）============
    {
        YOMK_CONTEXT_ON_MONITOR();
        auto cr = YOMK_CONTEXT_CREATE("mon_conc", YomkMkPtr(String, std::string("c_init")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_conc 成功");

        g_asyncCalls.store(0);
        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            g_asyncOrder.clear();
        }
        YOMK_CONTEXT_SET_MONITOR("mon_conc", [](const yomk::Context &ctx)
                                 {
            std::string v = contextStringValue(ctx);
            {
                std::lock_guard<std::mutex> lk(g_asyncMutex);
                g_asyncOrder.push_back(v);
            }
            ++g_asyncCalls; }, /*async=*/true);

        // N 个线程并发 set（各值可辨识）；C1 下异步入队在写锁内完成 ⇒ 入队序=提交序，
        // 单线程 FIFO 池 ⇒ 末条异步通知 = 最后一次提交的值 = 最终 CONTEXT_GET 值。
        // 旧实现（post 在解锁后）可能倒挂：先提交者后入队 ⇒ 末条 ≠ 终值。
        const int kThreads = 8;
        const int kIters = 20;
        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            workers.emplace_back([t, kIters]()
                                 {
                for (int i = 0; i < kIters; ++i)
                {
                    std::string v = "t" + std::to_string(t) + "_i" + std::to_string(i);
                    YOMK_CONTEXT_SET("mon_conc", YomkMkPtr(String, v));
                } });
        }
        for (auto &w : workers)
        {
            w.join();
        }

        const int kTotal = kThreads * kIters;
        bool reached = waitForCount(g_asyncCalls, kTotal, 15000);
        CHECK(reached, "并发 set 后异步 monitor 在等待窗口内全部触发（计数 == 总提交数）");

        // 所有写线程已 join，CONTEXT_GET 终值稳定
        auto finalVal = YOMK_CONTEXT_GET(String, "mon_conc", YomkMkPtr(String, std::string("default")));
        CHECK(finalVal && !finalVal->d.empty(), "读取 mon_conc 终值成功");

        {
            std::lock_guard<std::mutex> lk(g_asyncMutex);
            CHECK(g_asyncOrder.size() == static_cast<size_t>(kTotal), "异步顺序记录条数 == 总提交数");
            CHECK(!g_asyncOrder.empty() && finalVal && g_asyncOrder.back() == finalVal->d,
                  "并发 set 下异步末条 == 最终值（锁内入队 ⇒ 末条收敛真值）");
        }
        YOMK_CONTEXT_OFF_MONITOR();
    }

    // ============ Section 13: 异步 monitor 排空（deinit），末尾调用 shutdown ============
    {
        YOMK_CONTEXT_ON_MONITOR();
        auto cr = YOMK_CONTEXT_CREATE("mon_drain", YomkMkPtr(String, std::string("d0")));
        CHECK(cr.m_status == YomkResponse::eOk, "创建 mon_drain 成功");

        g_drainCalls.store(0);
        YOMK_CONTEXT_SET_MONITOR("mon_drain", [](const yomk::Context &)
                                 { ++g_drainCalls; }, /*async=*/true);

        const int N = 50;
        for (int i = 0; i < N; ++i)
        {
            YOMK_CONTEXT_SET("mon_drain", YomkMkPtr(String, std::string("d") + std::to_string(i)));
        }

        // 立即 shutdown，触发 Context deinit 排空 monitor 池
        YOMK_SHUTDOWN();

        // shutdown 返回后排空已完成，计数应等于投递次数
        CHECK(g_drainCalls.load() == N, "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到、无丢失）");
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
