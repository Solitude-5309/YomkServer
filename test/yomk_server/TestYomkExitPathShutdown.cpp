// 显式 shutdown 路径 + atexit 幂等对照测试：deinit 内域调用在两条退出路径上的语义钉扎
//
// 缺陷背景（详见 tmp/YomkServer 缺陷报告.md 第九章）：
// 修复前 atexit 路径上 deinit 域调用命中已析构 TLS 残留缓存导致 double free；
// 显式 shutdown 路径恰好因 setServer(nullptr) 的 epoch 失效而安全。两条路径
// 的 deinit 域入口语义并不相同，本测试钉扎各自现状并守护 atexit 幂等：
//
// 覆盖内容：
// 1. 显式 shutdown 期间 deinit 域入口返回"仍存活"的 server（此刻 holder 未清、
//    epoch 未变，快照为活对象；shutdown 先锁内拷出清空服务表再锁外 deinit，
//    deinit 内 delService 命中 "not found" 优雅返回 -1）——与 atexit 路径的
//    "未初始化"语义不同，契约差异文档化
// 2. shutdown 返回后守卫回归：instance() 空、create()=-1、status()=Invalid
// 3. shutdown 幂等：连续两次 shutdown 仅一次 deinit 清点
// 4. shutdown 后 init 再调：call_once 空转 + WARN，不崩
// 5. atexit 幂等：显式 shutdown 后 main 自然退出，destroy() 对空 holder 空转、
//    不再触发第二次 deinit（早注册 atexit 处理器断言 deinitCount 仍为 1、
//    闸门已置位）；全程退出码 0 即无 double free
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

// ---- 文件级观测变量 ----
static std::atomic<int> g_deinitCount{0};          // deinit 执行次数（期望恒为 1）
static std::atomic<bool> g_deinitInstanceAlive{false};  // 显式 shutdown 期间 deinit 内 server 存活
static std::atomic<int> g_deinitRemoveRet{1};      // deinit 内 remove() 返回值（期望 -1，not found）
static std::atomic<bool> g_postShutdownEmpty{false};    // shutdown 返回后 instance() 为空

// deinit 走域入口（显式 shutdown 路径下此刻 server 仍存活，返回 -1 而非"未初始化"）
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
        g_deinitInstanceAlive.store(yomk::server().instance() != nullptr);
        g_deinitRemoveRet.store(yomk::server().srv_name("/SvcGhost/ping").remove());
        g_deinitCount.fetch_add(1);
    }
    yomk::payload::Response ping(YomkMsgPtr)
    {
        return {yomk::payload::ResStatus::Succeeded, "pong"};
    }
};

// ---- 早注册 atexit：晚于 destroy 执行，断言显式 shutdown 后的退出终态 ----
static void atexitEarlyPostDestroy()
{
    const bool ok = YomkServerRef::destroyed().load(std::memory_order_acquire) &&
                    g_deinitCount.load() == 1 &&
                    yomk::server().instance() == nullptr;
    if (!ok)
    {
        std::cout << "[FAIL] [atexitEarlyPostDestroy] destroyed="
                  << YomkServerRef::destroyed().load()
                  << " deinitCount=" << g_deinitCount.load() << std::endl;
        std::_Exit(42);
    }
    std::cout << "[ OK ] [atexitEarlyPostDestroy] atexit destroy idempotent, deinit ran exactly once" << std::endl;
}

int main()
{
    if (std::atexit(atexitEarlyPostDestroy) != 0)
    {
        std::cout << "[FAIL] atexit(early) registration" << std::endl;
        return 1;
    }

    yomk::server().init();
    yomk::server().srv<SvcDomainEntry>().create();

    // 健全性：请求往返成功
    const auto resp = yomk::server().url("/SvcDomainEntry/ping").msg(nullptr).request();
    YOMK_TEST_CHECK(resp.status == yomk::payload::ResStatus::Succeeded, "sync request round-trip succeeds");

    // 1. 显式 shutdown：deinit 在 shutdown 内执行，域入口返回仍存活的 server
    yomk::server().shutdown();
    YOMK_TEST_CHECK(g_deinitCount.load() == 1, "deinit ran exactly once during shutdown");
    YOMK_TEST_CHECK(g_deinitInstanceAlive.load(), "domain entry inside deinit returns live server (pre-null holder)");
    YOMK_TEST_CHECK(g_deinitRemoveRet.load() == -1, "delService inside deinit returns -1 (table already cleared)");

    // 2. shutdown 返回后守卫回归
    g_postShutdownEmpty.store(yomk::server().instance() == nullptr);
    YOMK_TEST_CHECK(g_postShutdownEmpty.load(), "instance() null after shutdown");
    YOMK_TEST_CHECK(yomk::server().srv<SvcDomainEntry>().create() == -1, "create() returns -1 after shutdown");
    YOMK_TEST_CHECK(yomk::server().url("/SvcDomainEntry/ping").msg(nullptr).status() ==
                        yomk::payload::ResStatus::Invalid,
                    "status() returns Invalid after shutdown");

    // 3. shutdown 幂等：二次调用不再触发 deinit
    yomk::server().shutdown();
    YOMK_TEST_CHECK(g_deinitCount.load() == 1, "second shutdown does not re-run deinit");

    // 4. shutdown 后 init 再调：call_once 空转 + WARN，不崩
    yomk::server().init();
    YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "re-init after shutdown stays uninitialized");

    std::cout << "-- main returning after explicit shutdown; atexit destroy idempotence follows --" << std::endl;
    return g_failed == 0 ? 0 : 1;
    // 退出链：atexit destroy 对空 holder 空转并置位闸门 -> 早注册处理器断言终态
}
