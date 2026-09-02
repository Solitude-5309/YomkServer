/**
 * @file TestYomkConcurrency.cpp
 * @brief 多线程并发功能测试与 10 万级持续压测（正式全面测试，MC7）
 *
 * 覆盖内容：
 * 1. 并发同步 request 守恒（服务表/函数表 shared_mutex 读锁 + 副本保活）
 * 2. 并发 asyncRequest 不丢不重（请求池 + 服务表并发）
 * 3. request × add/del 并发：返回值合法集合内、无崩溃无悬垂
 * 4. request × installFunc 覆盖安装并发：function 副本原子替换无撕裂
 * 5. shutdown × 在途异步并发：排空完成后回调数稳定、shutdown 后投递全拒
 * 6. 删除即停并发形态：delService 后弱绑定回调立即丢弃、在途函数体收尾后停止
 * 7. N7 持续压测：8 线程 × 12500 次 ≈ 10 万级混合操作跨两个 server 实例
 *
 * 说明：类级并发测试不触 YOMK_INIT（与 MC5 同结论），单可执行文件多实例完成；
 *       时序敏感用例设有限等待窗口与守护上限，失败形态为断言非挂起；零源码变更
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

// ---- 文件级观测变量（每节首重置） ----
static std::atomic<int> g_hit{0};                        // 用户函数体真实执行计数
static std::atomic<int> g_fired{0};                      // 异步回调到达计数
static std::atomic<int> g_rejected{0};                   // 删除后弱绑定丢弃回调计数
static std::atomic<int> g_illegal{0};                    // 非法返回值计数
static std::atomic<int> g_posted{0};                     // 异步投递调用计数
static std::function<YomkResponse(YomkPkgPtr)> g_weakCb; // 服务 init 时存下的弱绑定回调

static void resetGlobals()
{
    g_hit.store(0);
    g_fired.store(0);
    g_rejected.store(0);
    g_illegal.store(0);
    g_posted.store(0);
    g_weakCb = nullptr;
}

static bool waitFor(std::function<bool()> pred, int timeoutMs, const std::string &desc)
{
    for (int i = 0; i < timeoutMs / 10; ++i)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "[TIMEOUT] " << desc << " (timeout " << timeoutMs << " ms)" << std::endl;
    return false;
}

/**
 * @brief 并发演示服务：/ping 固定响应、/echo String 回显；init 存下弱绑定回调
 */
class ConcSrv : public YomkService
{
public:
    explicit ConcSrv(YomkServer *server = nullptr)
        : YomkService(server) {}

    int init() override
    {
        YomkInstallFunc("/ping", ConcSrv::ping);
        YomkInstallFunc("/echo", ConcSrv::echo, String);
        g_weakCb = YomkBindWeakSelf(ConcSrv::ping);
        return 0;
    }

    YomkResponse ping(YomkPkgPtr)
    {
        ++g_hit;
        return {YomkResponse::eOk, "pong"};
    }

    YomkResponse echo(YomkPkgPtr pkg)
    {
        ++g_hit;
        YomkUnPackPkgResponse(pkg, String, sp);
        return {YomkResponse::eOk, sp->d};
    }
};

static int registerConcSrv(std::shared_ptr<YomkServer> server, const std::string &name)
{
    ConcSrv *srv = new ConcSrv(server.get());
    srv->name(name);
    return server->addService(srv);
}

static bool isExpectedNoResponse(const YomkResponse &r)
{
    if (r.m_status != YomkResponse::eNo)
        return false;
    // 并发注册窗口：YomkServer::addService 先入表后 init，request 可能在函数安装前命中服务，
    // 返回 function not found；这是框架设计窗口（非崩溃/悬垂缺陷），纳入可接受集合
    return r.m_msg.find("service not found") != std::string::npos ||
           r.m_msg.find("function not found") != std::string::npos ||
           r.m_msg.find("deleted or unregistered") != std::string::npos;
}

int main()
{
    std::cout << "===== 1. 并发同步 request（读写锁 + 副本保活基线） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        CHECK(registerConcSrv(server, "/PingSrv") == 0, "1.1 register /PingSrv");

        const int kThreads = 8;
        const int kPerThread = 500;
        std::atomic<int> okCount{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t)
        {
            workers.emplace_back([&]
                                 {
                for (int i = 0; i < kPerThread; ++i)
                {
                    if (i % 2 == 0)
                    {
                        auto r = server->request("/PingSrv/ping");
                        if (r.m_status == YomkResponse::eOk && r.m_msg == "pong")
                            ++okCount;
                    }
                    else
                    {
                        auto r = server->request("/PingSrv/echo", YomkMkPtr(String, "x"));
                        if (r.m_status == YomkResponse::eOk && r.m_msg == "x")
                            ++okCount;
                    }
                } });
        }
        for (auto &th : workers)
            th.join();

        CHECK(okCount.load() == kThreads * kPerThread, "1.2 concurrent sync request all eOk");
        CHECK(g_hit.load() == kThreads * kPerThread, "1.3 hit count equals total requests");
        server->shutdown();
    }

    std::cout << "===== 2. 并发 asyncRequest（请求池不丢不重） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        CHECK(registerConcSrv(server, "/AsyncSrv") == 0, "2.1 register /AsyncSrv");

        const int kThreads = 8;
        const int kPerThread = 125;
        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t)
        {
            workers.emplace_back([&]
                                 {
                for (int i = 0; i < kPerThread; ++i)
                {
                    server->asyncRequest("/AsyncSrv/echo", YomkMkPtr(String, "a"),
                                         [](YomkResponse r) {
                                             if (r.m_status == YomkResponse::eOk)
                                                 ++g_fired;
                                         });
                } });
        }
        for (auto &th : workers)
            th.join();

        bool ok = waitFor([&]
                          { return g_fired.load() == kThreads * kPerThread; }, 5000,
                          "async callbacks all fired");
        CHECK(ok, "2.2 all async callbacks fired after join (no loss/dup)");
        server->shutdown();
    }

    std::cout << "===== 3. request × add/del 并发（服务表读写锁） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        CHECK(registerConcSrv(server, "/PingSrv") == 0, "3.1 register /PingSrv");

        const int kReqThreads = 8;
        std::atomic<int> okCount{0};
        std::atomic<bool> stop{false};
        std::vector<std::thread> workers;
        for (int t = 0; t < kReqThreads; ++t)
        {
            workers.emplace_back([&]
                                 {
                while (!stop.load())
                {
                    auto r = server->request("/PingSrv/ping");
                    if (r.m_status == YomkResponse::eOk)
                        ++okCount;
                    else if (!isExpectedNoResponse(r))
                        ++g_illegal;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                } });
        }

        for (int round = 0; round < 20; ++round)
        {
            server->delService("/PingSrv");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            registerConcSrv(server, "/PingSrv");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        stop.store(true);
        for (auto &th : workers)
            th.join();

        CHECK(g_illegal.load() == 0, "3.2 no illegal responses during add/del concurrency");
        CHECK(okCount.load() > 0, "3.3 alive window served requests");
        server->shutdown();
    }

    std::cout << "===== 4. request × installFunc 覆盖安装并发（函数表读写锁） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        ConcSrv *echoSrv = new ConcSrv(server.get());
        echoSrv->name("/EchoSrv");
        CHECK(server->addService(echoSrv) == 0, "4.1 register /EchoSrv");

        const int kReqThreads = 8;
        std::atomic<int> okCount{0};
        std::atomic<bool> stop{false};
        std::vector<std::thread> workers;
        for (int t = 0; t < kReqThreads; ++t)
        {
            workers.emplace_back([&]
                                 {
                while (!stop.load())
                {
                    auto r = server->request("/EchoSrv/echo", YomkMkPtr(String, "loop"));
                    if (r.m_status == YomkResponse::eOk && r.m_msg == "loop")
                        ++okCount;
                    else
                        ++g_illegal;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                } });
        }

        for (int round = 0; round < 200; ++round)
        {
            echoSrv->installFunc("/echo",
                                 echoSrv->weakFunc(std::bind(&ConcSrv::echo, echoSrv, std::placeholders::_1)),
                                 "String");
            echoSrv->installFunc("/echo",
                                 echoSrv->weakFunc(std::bind(&ConcSrv::echo, echoSrv, std::placeholders::_1)));
        }
        stop.store(true);
        for (auto &th : workers)
            th.join();

        CHECK(g_illegal.load() == 0, "4.2 no illegal responses during installFunc concurrency");
        CHECK(okCount.load() > 0, "4.3 request survived concurrent overwrite install");
        server->shutdown();
    }

    std::cout << "===== 5. shutdown × 在途异步并发（真并发交错） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        CHECK(registerConcSrv(server, "/ShutdownAsyncSrv") == 0,
              "5.1 register /ShutdownAsyncSrv");

        g_posted.store(0);
        g_fired.store(0);
        std::atomic<bool> shutdownDone{false};
        std::thread poster([&]
                           {
            while (!shutdownDone.load())
            {
                server->asyncRequest("/ShutdownAsyncSrv/echo", YomkMkPtr(String, "s"),
                                     [](YomkResponse r) {
                                         if (r.m_status == YomkResponse::eOk)
                                             ++g_fired;
                                     });
                ++g_posted;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        server->shutdown();
        shutdownDone.store(true);
        poster.join();

        int last = g_fired.load();
        bool stable = waitFor(
            [&]
            {
                int cur = g_fired.load();
                if (cur == last)
                    return true;
                last = cur;
                return false;
            },
            5000, "async fired stable after shutdown");
        CHECK(stable, "5.2 async callbacks stable after shutdown (drain complete)");
        CHECK(g_fired.load() <= g_posted.load(), "5.3 no duplicate async execution");

        int before = g_fired.load();
        for (int i = 0; i < 100; ++i)
        {
            server->asyncRequest("/ShutdownAsyncSrv/echo", YomkMkPtr(String, "s"),
                                 [](YomkResponse r)
                                 {
                                     if (r.m_status == YomkResponse::eOk)
                                         ++g_fired;
                                 });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(g_fired.load() == before, "5.4 post-shutdown async requests rejected");
    }

    std::cout << "===== 6. 删除即停并发形态（弱绑定判活） =====" << std::endl;
    {
        resetGlobals();
        auto server = YomkServer::create(4);
        CHECK(registerConcSrv(server, "/StopSrv") == 0, "6.1 register /StopSrv");
        CHECK(g_weakCb != nullptr, "6.2 weak callback installed in init");

        g_posted.store(0);
        g_fired.store(0);
        g_rejected.store(0);
        g_hit.store(0);
        std::atomic<bool> afterDel{false};
        std::thread worker([&]
                           {
            while (!afterDel.load())
            {
                YomkResponse r = g_weakCb(nullptr);
                if (r.m_status == YomkResponse::eOk && r.m_msg == "pong")
                    ++g_fired;
                else if (r.m_status == YomkResponse::eNo &&
                         r.m_msg.find("deleted or unregistered") != std::string::npos)
                    ++g_rejected;
                ++g_posted;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->delService("/StopSrv");
        // 给 worker 留约 500ms 窗口观测删除后的丢弃回调
        for (int i = 0; i < 100 && g_rejected.load() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        afterDel.store(true);
        worker.join();

        int lastHit = g_hit.load();
        bool stable = waitFor(
            [&]
            {
                int cur = g_hit.load();
                if (cur == lastHit)
                    return true;
                lastHit = cur;
                return false;
            },
            5000, "user hit count stable after delService");
        CHECK(stable, "6.3 user function hit count stable after del (in-flight finished)");
        CHECK(g_rejected.load() > 0, "6.4 post-del weak callbacks discarded with contract message");
        server->shutdown();
    }

    std::cout << "===== 7. N7 持续压测（8 线程 × 12500 ≈ 10 万级混合操作） =====" << std::endl;
    {
        resetGlobals();
        auto server1 = YomkServer::create(4);
        auto server2 = YomkServer::create(4);
        CHECK(registerConcSrv(server1, "/StressSrv") == 0, "7.1 register server1 /StressSrv");
        CHECK(registerConcSrv(server2, "/StressSrv") == 0, "7.2 register server2 /StressSrv");

        std::atomic<int> posted1{0}, posted2{0}, fired1{0}, fired2{0}, ok1{0}, ok2{0};
        std::atomic<bool> shutdown2Done{false};
        const int kThreadsPerServer = 4;
        const int kOpsPerThread = 12500;

        auto worker = [&](std::shared_ptr<YomkServer> server, std::atomic<int> *posted,
                          std::atomic<int> *fired, std::atomic<int> *ok)
        {
            for (int i = 0; i < kOpsPerThread; ++i)
            {
                int mod = i % 3;
                if (mod == 0)
                {
                    auto r = server->request("/StressSrv/ping");
                    if (r.m_status == YomkResponse::eOk)
                        ++(*ok);
                }
                else if (mod == 1)
                {
                    server->asyncRequest("/StressSrv/echo", YomkMkPtr(String, "stress"),
                                         [fired](YomkResponse r)
                                         {
                                             if (r.m_status == YomkResponse::eOk)
                                                 ++(*fired);
                                         });
                    ++(*posted);
                }
                else
                {
                    (void)server->serviceNames();
                }
            }
        };

        std::vector<std::thread> workers;
        for (int t = 0; t < kThreadsPerServer; ++t)
        {
            workers.emplace_back(worker, server1, &posted1, &fired1, &ok1);
            workers.emplace_back(worker, server2, &posted2, &fired2, &ok2);
        }
        std::thread shutdownThread([&]
                                   {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            server2->shutdown();
            server2->shutdown(); // 并发幂等顺带验证
            shutdown2Done.store(true); });

        for (auto &th : workers)
            th.join();
        shutdownThread.join();

        // server1 未 shutdown，等待所有 async 回调完成
        CHECK(waitFor([&]
                      { return fired1.load() == posted1.load(); }, 10000,
                      "server1 async callbacks complete"),
              "7.3 server1 async callbacks all fired");

        // server2 已 shutdown，等待回调数稳定（可能小于 posted2）
        int last2 = fired2.load();
        CHECK(waitFor(
                  [&]
                  {
                      int cur = fired2.load();
                      if (cur == last2)
                          return true;
                      last2 = cur;
                      return false;
                  },
                  10000, "server2 async callbacks stable after shutdown"),
              "7.4 server2 async callbacks stable after shutdown");

        CHECK(ok1.load() > 0, "7.5 server1 stress requests got eOk");
        CHECK(fired1.load() <= posted1.load(), "7.6 server1 async no duplicate execution");
        CHECK(fired2.load() <= posted2.load(), "7.7 server2 async no duplicate execution after shutdown");
        // server1 由作用域析构兜底；server2 已显式 shutdown
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
