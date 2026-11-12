// atexit 真实退出路径回归测试：deinit 内调用域入口 + 进程自然退出收尾
//
// 缺陷背景（详见 tmp/YomkServer 缺陷报告.md 第九章）：
// thread_local cached 是普通 TLS 对象，析构早于 atexit 回调（退出链 LIFO）；
// 修复前 atexit destroy() 期间 deinit 内走域入口（yomk::server()）会命中已析构
// TLS 的残留悬垂缓存，在已释放控制块上加减引用计数，返回时二次 dispose 触发
// double free（EXIT=134）。修复：destroy() 首步置位 destroyed() 闸门，
// serverSnapshot() 首行短路，全程不触碰已析构缓存。
//
// 覆盖内容：
// 1. deinit 域入口语义：instance() 为空、remove()=-1、status()=Invalid、
//    create()=-1、shutdown() 空转——全部优雅返回，不崩不挂
// 2. atexit 终态断言：早注册的 atexit 处理器（LIFO 晚于 destroy 执行）校验
//    闸门已置位、快照为空、deinit 计数完整；失败 _exit(42) 固定退出码
// 3. 后注册 atexit 处理器内域入口可用（先于 destroy 执行，server 存活）——
//    闸门不得误伤正常退出收尾中的合法域调用
// 4. 多线程退出窗口 + 在途持有者推迟 delete 的加固实证：已 join 的压力线程
//    缓存正常生灭；分离线程在 destroy 期间持续持有/释放快照副本（正是会推迟
//    delete 的场景），destroy 显式 local->shutdown() 保证 deinit sweep 同步完成，
//    终态断言不受其影响——ASan 下暴露任何残留 UAF
// 5. shutdown 期间 deinit 内 delService 不产生迭代器失效：shutdown 先在锁内
//    拷出并清空服务表再锁外逐个 deinit（YomkServerPrivate.cpp），deinit 内
//    delService 只会命中 "not found"——实证不崩
// 6. 裸指针直调收尾模式（YomkService.h 推荐写法）：deinit 经注入的 YomkServer*
//    直调 delService 优雅返回 -1
//
// 关键约束：本程序必须走真实退出路径（main 直接 return），不得先显式 shutdown，
// 否则 atexit 闸门时序测不到。双保险断言：double free 由退出码非 0 暴露；
// 终态语义由早注册 atexit 处理器 _exit(42) 暴露。
//
// 说明：call_once 单例限本程序一次性 init；显式 shutdown 路径的对照语义
// 归 TestYomkExitPathShutdown；api 级生命周期归 TestYomkAPILifecycle
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "yomk/server/YomkServerApi.h"
#include "yomk/server/YomkServerRef.h"

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

// ---- 文件级观测变量（deinit 写于退出收尾期、读于早注册 atexit 处理器；
//      处理器串行执行无并发，统一 atomic 保守处理） ----
static std::atomic<int> g_deinitCount{0};          // 走到 deinit 的服务数（期望 2）
static std::atomic<bool> g_deinitInstanceNull{false};   // deinit 内 instance() 为空
static std::atomic<int> g_deinitRemoveRet{1};      // deinit 内 remove() 返回值（期望 -1）
static std::atomic<int> g_deinitCreateRet{1};      // deinit 内 create() 返回值（期望 -1）
static std::atomic<bool> g_deinitStatusInvalid{false};  // deinit 内 status() == Invalid
static std::atomic<int> g_deinitShutdownCalls{0};  // deinit 内 shutdown() 空转次数（不崩即可）
static std::atomic<int> g_rawDeregRet{1};          // deinit 内裸指针 delService 返回值（期望 -1）
static std::atomic<int> g_looperSnapshotHits{0};   // 分离线程退出窗口期取到的非空快照数
static std::atomic<bool> g_lateAtexitSawServer{false};  // 后注册 atexit 内 server 存活

// ---- 测试服务 ----

// 1+2：deinit 走全部域入口（修复前此处 double free）
class SvcDomainEntry : public YomkService
{
public:
    explicit SvcDomainEntry(YomkServer* s) : YomkService(s) { name("/SvcDomainEntry"); }
    int init() override
    {
        installFunc("/ping", &SvcDomainEntry::ping);
        return 0;
    }
    void deinit() override
    {
        g_deinitInstanceNull.store(yomk::server().instance() == nullptr);
        g_deinitRemoveRet.store(yomk::server().srv_name("/SvcGhost").remove());
        g_deinitCreateRet.store(yomk::server().srv<SvcDomainEntry>().create());
        g_deinitStatusInvalid.store(yomk::server().url("/SvcGhost/ping").msg(nullptr).status() ==
                                    yomk::payload::ResStatus::Invalid);
        yomk::server().shutdown(); // deinit 内再 shutdown：闸门短路空转，不得崩
        g_deinitShutdownCalls.fetch_add(1);
        g_deinitCount.fetch_add(1);
    }
    yomk::payload::Response ping(YomkMsgPtr)
    {
        return {yomk::payload::ResStatus::Succeeded, "pong"};
    }
};

// 5：shutdown 排空语义验证对象——被表清空后逐个 deinit，自身不再注销别人
class SvcPeer : public YomkService
{
public:
    explicit SvcPeer(YomkServer* s) : YomkService(s) { name("/SvcPeer"); }
    int init() override
    {
        installFunc("/ping", &SvcPeer::ping);
        return 0;
    }
    void deinit() override { g_deinitCount.fetch_add(1); }
    yomk::payload::Response ping(YomkMsgPtr)
    {
        return {yomk::payload::ResStatus::Succeeded, "peer-pong"};
    }
};

// 6：裸指针直调收尾模式（YomkService.h 推荐写法）：deinit 经注入指针直调 delService
class SvcRawDereg : public YomkService
{
public:
    explicit SvcRawDereg(YomkServer* s) : YomkService(s), m_server(s) { name("/SvcRawDereg"); }
    int init() override { return 0; }
    void deinit() override
    {
        // shutdown 已清空服务表，注销不存在服务优雅返回 -1（不崩即达标）
        if (m_server)
        {
            g_rawDeregRet.store(m_server->delService("/SvcGhost"));
        }
    }
    yomk::payload::Response ping(YomkMsgPtr)
    {
        return {yomk::payload::ResStatus::Succeeded, "ok"};
    }

private:
    YomkServer* m_server = nullptr;
};

// ---- 早注册 atexit：LIFO 上晚于 init 内注册的 destroy 执行，断言退出终态 ----
// 失败 _exit(42) 固定退出码（此时 main 的返回值已定，只能借 _exit 报告）
static void atexitEarlyPostDestroy()
{
    const bool ok = YomkServerRef::destroyed().load(std::memory_order_acquire) &&
                    g_lateAtexitSawServer.load() &&
                    g_deinitCount.load() == 2 &&
                    g_deinitInstanceNull.load() &&
                    g_deinitRemoveRet.load() == -1 &&
                    g_deinitCreateRet.load() == -1 &&
                    g_deinitStatusInvalid.load() &&
                    g_deinitShutdownCalls.load() == 1 &&
                    g_rawDeregRet.load() == -1 &&
                    yomk::server().instance() == nullptr;
    if (!ok)
    {
        std::cout << "[FAIL] [atexitEarlyPostDestroy] post-destroy state: "
                  << "destroyed=" << YomkServerRef::destroyed().load()
                  << " deinitCount=" << g_deinitCount.load()
                  << " instanceNull=" << g_deinitInstanceNull.load()
                  << " removeRet=" << g_deinitRemoveRet.load()
                  << " createRet=" << g_deinitCreateRet.load()
                  << " statusInvalid=" << g_deinitStatusInvalid.load()
                  << " rawDeregRet=" << g_rawDeregRet.load()
                  << " looperHits=" << g_looperSnapshotHits.load()
                  << std::endl;
        std::_Exit(42);
    }
    std::cout << "[ OK ] [atexitEarlyPostDestroy] post-destroy final state verified" << std::endl;
}

// ---- 后注册 atexit：LIFO 上先于 destroy 执行，server 此刻仍存活 ----
static void atexitLatePreDestroy()
{
    g_lateAtexitSawServer.store(yomk::server().instance() != nullptr);
}

// ---- 分离线程：退出窗口并发压测（闸门短路返回空） ----
// 故意在 destroy 执行期间持续持有/释放快照副本：修复"在途持有者推迟 delete"
// 之前的框架里，若 reset() 瞬间恰有副本，delete 连带 deinit sweep 被推迟（极端
// 下进程消亡、deinit 整体跳过）；修复后 destroy() 显式 local->shutdown() 同步
// 排空，deinit 必然在 atexit 处理器内完成，本线程的引用持有不再影响终态断言。
// 同时压测闸门短路路径与控制块并发读写（修复前命中已析构 TLS 缓存，ASan 下
// 必现 UAF）
static void exitWindowLooper()
{
    for (;;)
    {
        std::shared_ptr<YomkServer> s = yomk::server().instance(); // 拷贝 + 析构，压控制块
        if (s)
        {
            g_looperSnapshotHits.fetch_add(1); // 仅观测：闸门前置位命中属正常
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

int main()
{
    // 先于 init 注册：退出链 LIFO 使本处理器晚于 destroy 执行，可断言终态
    if (std::atexit(atexitEarlyPostDestroy) != 0)
    {
        std::cout << "[FAIL] atexit(early) registration" << std::endl;
        return 1;
    }

    yomk::server().init();

    // 晚于 init 注册：先于 destroy 执行，server 存活，域入口合法可用
    if (std::atexit(atexitLatePreDestroy) != 0)
    {
        std::cout << "[FAIL] atexit(late) registration" << std::endl;
        return 1;
    }

    yomk::server().srv<SvcDomainEntry>().create();
    yomk::server().srv<SvcPeer>().create();
    yomk::server().srv<SvcRawDereg>().create();

    // 健全性：server 功能正常（请求往返成功）
    const auto resp = yomk::server().url("/SvcPeer/ping").msg(nullptr).request();
    YOMK_TEST_CHECK(resp.status == yomk::payload::ResStatus::Succeeded, "sync request round-trip succeeds");

    // 已 join 压力线程：每线程缓存正常生灭（epoch 不变，快照全命中热路径）
    std::atomic<int> joinedHits{0};
    auto stressFn = [&joinedHits]()
    {
        for (int i = 0; i < 20000; ++i)
        {
            if (yomk::server().instance())
            {
                joinedHits.fetch_add(1);
            }
        }
    };
    std::thread t1(stressFn);
    std::thread t2(stressFn);
    std::thread t3(stressFn);
    t1.join();
    t2.join();
    t3.join();
    YOMK_TEST_CHECK(joinedHits.load() == 60000, "joined stress threads all got live snapshots (60000/60000)");

    // 分离线程：故意存活到退出窗口，压测 destroy 期间的并发快照
    std::thread looper(exitWindowLooper);
    looper.detach();
    YOMK_TEST_CHECK(true, "detached exit-window looper spawned");

    std::cout << "-- main returning without explicit shutdown; atexit destroy path follows --" << std::endl;
    return g_failed == 0 ? 0 : 1;
    // 退出链（LIFO）：main TLS cached 析构 -> atexitLatePreDestroy（server 存活）
    //   -> Holder::destroy（闸门置位 -> deinit 域入口全部短路 -> delete）
    //   -> atexitEarlyPostDestroy（终态断言）
}
