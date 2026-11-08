// 跨模块端到端集成测试（单模块测试无法覆盖的跨模块路径与 shutdown 统一收尾）
//
// 覆盖内容（3 个 Section，单进程一次 yomk::server().async_thread(1).init()，断言以运行为准）：
// S3. asyncRequest 回调内重入：异步 monitor 回调（监控池线程）内同步 yomk::ctx().key().set() 重入
// /YomkContext/set 并触发另一键的同步 monitor——不死锁、异步/同步计数守恒、
// 异步按提交序保序（末值可读）
// S2. 删除内置服务契约与恢复：yomk::server().srv_name("/YomkLogger").remove() → 服务级 ResStatus::Failed + 精确消息；
// yomk::server().srv<YomkLogger>().create() 无参重注册（new T(this) 注入 server 指针，构造函数
// 固定名 "/YomkLogger" 生效）→ LOGGER API 恢复且全链路可用
// S1. yomk::server().shutdown() 全链路统一收尾（压轴）：event_loop().post() 积压、log().to_sink(...).info 缓冲、
// 异步 monitor 投递三类在途状态一次性排空——monitor 计数守恒、文件落盘（FileLogger
// 析构 write）、API 回归 ResStatus::Invalid + "YomkServer is not init"（API 守卫级：shutdown 内
// setServer(nullptr)，serverSnapshot 空守卫；服务级 ResStatus::Failed 仅出现在服务表内服务缺失时）、
// 二次 shutdown 幂等
//
// 执行顺序约束：yomk::server().init() 为 static once_flag + call_once（单进程仅首次生效，shutdown 后
// 再 init 返回空快照不可逆），故 shutdown 段必须压轴，重入/删除恢复段在其前完成
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "Modules/Logger/YomkLogger.h"
#include "YomkApi.h"

namespace fs = std::filesystem;

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

static std::atomic<int> g_reentryAsyncCalls{0};
static std::atomic<int> g_reentrySyncCalls{0};
static std::atomic<int> g_drainCalls{0};

// 读取文本文件全部内容（打不开返回 false）
static bool readFile(const fs::path& path, std::string& out)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

// StringArray 响应解包（非 ResStatus::Succeeded/解包失败返回空表）
static std::vector<std::string> unpackStringArray(const yomk::payload::Response& resp)
{
    std::vector<std::string> names;
    if (resp.m_status != yomk::payload::ResStatus::Succeeded || !resp.m_data)
    {
        return names;
    }
    auto arr = std::dynamic_pointer_cast<yomk::msgs::StringArray>(resp.m_data);
    if (arr)
    {
        names = arr->d;
    }
    return names;
}

static bool contains(const std::vector<std::string>& vec, const std::string& item)
{
    for (auto& v : vec)
    {
        if (v == item)
        {
            return true;
        }
    }
    return false;
}

// 轮询限时等待条件成立（10ms 步进）
static bool waitUntil(const std::function<bool()>& cond, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += 10)
    {
        if (cond())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

// 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退 /tmp
static fs::path testTmpRoot()
{
    const char* env = std::getenv("YOMK_TEST_TMP_ROOT");
    return (env && *env) ? fs::path(env) : fs::temp_directory_path();
}

int main()
{
    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().async_thread(1).init() 返回非空服务器");

    const int K = 20;

    // ============ S3. asyncRequest 回调内重入（异步 monitor 回调同步重入 /YomkContext/set） ============
    std::cout << "===== S3. asyncRequest 回调内重入 =====" << std::endl;
    {
        YOMK_TEST_CHECK(yomk::ctx().group().monitor().enable() == yomk::payload::ResStatus::Succeeded, "开启 monitor 开关");
        YOMK_TEST_CHECK(yomk::ctx().key("reentry_key").value(yomk::msgs::String::shared(std::string("k0"))).create() == yomk::payload::ResStatus::Succeeded,
              "创建 reentry_key 成功");
        YOMK_TEST_CHECK(yomk::ctx().key("reentry_out").value(yomk::msgs::String::shared(std::string("out_init"))).create() == yomk::payload::ResStatus::Succeeded,
              "创建 reentry_out 成功");

        g_reentryAsyncCalls.store(0);
        g_reentrySyncCalls.store(0);

        // 异步 monitor：监控池线程内同步重入 /YomkContext/set（无捕获 lambda + 全局原子计数）
        yomk::ctx().key("reentry_key").monitor_fn([](const yomk::payload::Context&)
            {
                g_reentryAsyncCalls.fetch_add(1, std::memory_order_relaxed);
                yomk::ctx().key("reentry_out").value(yomk::msgs::String::shared(std::string("out_") + std::to_string(g_reentryAsyncCalls.load()))).set();
            }).async(/*async=*/true).apply();
        // 同步 monitor：重入 SET 的内联观察点（每次重入 SET 恰好触发一次）
        yomk::ctx().key("reentry_out").monitor_fn([](const yomk::payload::Context&)
            {
                g_reentrySyncCalls.fetch_add(1, std::memory_order_relaxed);
            }).apply();

        bool allSetOk = true;
        for (int i = 0; i < K; ++i)
        {
            if (yomk::ctx().key("reentry_key").value(yomk::msgs::String::shared(std::string("k") + std::to_string(i))).set() !=
                yomk::payload::ResStatus::Succeeded)
            {
                allSetOk = false;
            }
        }
        YOMK_TEST_CHECK(allSetOk, "K 次投递 SET 全部 ResStatus::Succeeded");

        YOMK_TEST_CHECK(waitUntil([&]() { return g_reentrySyncCalls.load() == K; }, 5000),
              "重入链路在限时内收敛（不死锁，测试自然完成即证）");
        YOMK_TEST_CHECK(g_reentryAsyncCalls.load() == K, "异步 monitor 计数守恒 == K");
        YOMK_TEST_CHECK(g_reentrySyncCalls.load() == K, "重入 SET 触发同步 monitor 计数守恒 == K");

        auto last = yomk::ctx().key("reentry_out").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string(""))).get();
        YOMK_TEST_CHECK(last != nullptr && last->d == "out_" + std::to_string(K),
              "重入写入末值可读（异步 monitor 按提交序保序，末次重入为 out_K）");

        bool cleanupOk = yomk::ctx().group().monitor().disable() == yomk::payload::ResStatus::Succeeded &&
                         yomk::ctx().key("reentry_key").destroy() == yomk::payload::ResStatus::Succeeded &&
                         yomk::ctx().key("reentry_out").destroy() == yomk::payload::ResStatus::Succeeded;
        YOMK_TEST_CHECK(cleanupOk, "S3 清场：OFF_MONITOR 与两键 DESTROY 全部 ResStatus::Succeeded");
    }

    // ============ S2. 删除内置服务契约与恢复（/YomkLogger） ============
    std::cout << "===== S2. 删除内置服务契约与恢复（/YomkLogger） =====" << std::endl;
    {
        YOMK_TEST_CHECK(yomk::server().srv_name("/YomkLogger").remove() == 0, "删除内置 /YomkLogger 返回 0");
        auto afterDel = yomk::log_mgr().console().list();
        YOMK_TEST_CHECK(afterDel.m_status == yomk::payload::ResStatus::Failed && afterDel.m_msg == "service not found: /YomkLogger",
              "删除后 LOGGER API 服务级 ResStatus::Failed + 精确消息");
        YOMK_TEST_CHECK(!contains(unpackStringArray(yomk::server().services()), "/YomkLogger"),
              "服务表快照不再含 /YomkLogger");

        YOMK_TEST_CHECK(yomk::server().srv<YomkLogger>().create() == 0, "newService 无参重注册返回 0（构造函数固定名生效）");
        auto restored = yomk::log_mgr().console().list();
        // loggers 按类型查询，行内容即 logger 纯名（YomkLogger::loggers 不再拼接类型后缀）
        YOMK_TEST_CHECK(restored.m_status == yomk::payload::ResStatus::Succeeded && contains(unpackStringArray(restored), "main"),
              "重注册后 LOGGER API 恢复且含 main");
        YOMK_TEST_CHECK(yomk::log().tag("main").info("[integration] s2 relink marker") ==
                  yomk::payload::ResStatus::Succeeded,
              "重注册后日志 API 全链路可用（console_log ResStatus::Succeeded）");
    }

    // ============ S1. yomk::server().shutdown() 全链路统一收尾（压轴：call_once 后不可再 init） ============
    std::cout << "===== S1. yomk::server().shutdown() 全链路统一收尾（压轴） =====" << std::endl;
    {
        fs::path tmpDir = testTmpRoot() / ("yomk_integration_s1_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::create_directories(tmpDir, ec);
        YOMK_TEST_CHECK(!ec, "S1 临时目录创建成功: " + tmpDir.string());

        // 事件积压（不等待执行完成；积压处理语义不保证全执行，仅验证 shutdown 有限时间返回）
        YOMK_TEST_CHECK(yomk::event_loop().loop("integration_loop").start() == yomk::payload::ResStatus::Succeeded, "启动 integration_loop");
        bool postOk = true;
        for (int i = 0; i < 100; ++i)
        {
            if (yomk::event_loop().msg(nullptr).loop("integration_loop").handle_fn([](YomkMsgPtr) { return yomk::payload::Response(yomk::payload::ResStatus::Succeeded); }).post() !=
                yomk::payload::ResStatus::Succeeded)
            {
                postOk = false;
            }
        }
        YOMK_TEST_CHECK(postOk, "100 个积压事件全部 POST ResStatus::Succeeded");

        // 文件日志：log().to_sink(...).info 仅入缓冲，FileLogger 析构 write 落盘
        // （file() 分面为日志器名，实际文件 = <dir>/<名>.log，由 FileLogger 自动追加后缀）
        YOMK_TEST_CHECK(yomk::log_mgr().dir(tmpDir.string()).file("integration_file").create() == yomk::payload::ResStatus::Succeeded,
              "file().create() 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::log().to_sink("integration_file").info("s1_shutdown_flush_marker") == yomk::payload::ResStatus::Succeeded,
              "log().to_sink(...).info 入缓冲返回 ResStatus::Succeeded（日志器名与 CREATE 一致）");

        // 异步 monitor 投递（shutdown 排空断言主体；S3 清场时全局开关已关，此处需重新开启）
        YOMK_TEST_CHECK(yomk::ctx().group().monitor().enable() == yomk::payload::ResStatus::Succeeded, "重新开启 monitor 开关");
        YOMK_TEST_CHECK(yomk::ctx().key("drain_key").value(yomk::msgs::String::shared(std::string("d0"))).create() == yomk::payload::ResStatus::Succeeded,
              "创建 drain_key 成功");
        g_drainCalls.store(0);
        yomk::ctx().key("drain_key").monitor_fn([](const yomk::payload::Context&) { g_drainCalls.fetch_add(1, std::memory_order_relaxed); }).async(/*async=*/true).apply();
        const int N = 50;
        bool drainSetOk = true;
        for (int i = 0; i < N; ++i)
        {
            if (yomk::ctx().key("drain_key").value(yomk::msgs::String::shared(std::string("d") + std::to_string(i))).set() !=
                yomk::payload::ResStatus::Succeeded)
            {
                drainSetOk = false;
            }
        }
        YOMK_TEST_CHECK(drainSetOk, "N 次异步通知投递全部 ResStatus::Succeeded");

        // 统一收尾：排空异步 monitor + 落盘文件日志 + 清空服务表
        yomk::server().shutdown();

        YOMK_TEST_CHECK(g_drainCalls.load() == N, "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到无丢失）");
        std::string fileContent;
        bool readOk = readFile(tmpDir / "integration_file.log", fileContent);
        YOMK_TEST_CHECK(readOk && fileContent.find("s1_shutdown_flush_marker") != std::string::npos,
              "shutdown 返回后文件日志已落盘且含标记串（析构 write）");

        // API 守卫级回归：serverSnapshot 已置空 → ResStatus::Invalid + "YomkServer is not init"
        YOMK_TEST_CHECK(yomk::event_loop().msg(nullptr).loop("integration_loop").handle_fn(YomkServiceFunc()).post() == yomk::payload::ResStatus::Invalid,
              "shutdown 后 event_loop().post() 回归 ResStatus::Invalid（失败原因经错误日志承载）");
        auto postCtx = yomk::ctx().key("drain_key").value(yomk::msgs::String::shared(std::string("x"))).set();
        YOMK_TEST_CHECK(postCtx == yomk::payload::ResStatus::Invalid,
              "shutdown 后 setContext 回归 ResStatus::Invalid");
        auto postLog = yomk::log_mgr().all();
        YOMK_TEST_CHECK(postLog.m_status == yomk::payload::ResStatus::Invalid && postLog.m_msg == "YomkServer is not init",
              "shutdown 后 LOGGER API 回归 ResStatus::Invalid + 守卫消息");
        auto postInfo = yomk::server().all();
        YOMK_TEST_CHECK(postInfo.m_status == yomk::payload::ResStatus::Invalid && postInfo.m_msg == "YomkServer is not init",
              "shutdown 后 SERVER_INFO API 回归 ResStatus::Invalid + 守卫消息");

        // 跨线程守卫语义：shutdown 返回后另起线程调用，epoch 已传播（happens-before
        // 链：setServer 锁外 fetch_add(release) → 本线程 join 前创建子线程 → 子线程
        // acquire load），确定性走慢路径拿到空快照 → 守卫消息逐字节保留
        {
            yomk::payload::Response crossResp(yomk::payload::ResStatus::Succeeded);
            std::thread worker([&crossResp]() { crossResp = yomk::log_mgr().all(); });
            worker.join();
            YOMK_TEST_CHECK(crossResp.m_status == yomk::payload::ResStatus::Invalid &&
                                crossResp.m_msg == "YomkServer is not init",
                  "shutdown 后跨线程 LOGGER API 回归 ResStatus::Invalid + 守卫消息（epoch 传播）");
        }

        yomk::server().shutdown();  // 幂等：二次调用空转安全
        YOMK_TEST_CHECK(true, "shutdown 二次调用幂等无崩溃");

        fs::remove_all(tmpDir, ec);
    }

    std::cout << (g_failed == 0 ? "TestYomkIntegration all check passed." : "TestYomkIntegration FAILED")
              << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
