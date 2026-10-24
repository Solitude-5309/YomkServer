/**
 * @file TestYomkIntegration.cpp
 * @brief 跨模块端到端集成测试（正式全面测试，P3——单模块测试无法覆盖的跨模块路径与 shutdown 统一收尾）
 *
 * 覆盖内容（3 个 Section，单进程一次 YOMK_INIT(1)，断言以运行为准）：
 * S3. asyncRequest 回调内重入：异步 monitor 回调（监控池线程）内同步 YOMK_CONTEXT_SET 重入
 *     /YomkContext/set 并触发另一键的同步 monitor——不死锁、异步/同步计数守恒、
 *     异步按提交序保序（末值可读）
 * S2. 删除内置服务契约与恢复：YOMK_DEL_SERVICE("/YomkLogger") → 服务级 eNo + 精确消息；
 *     YomkAPI::newService<YomkLogger>() 无参重注册（new T(this) 注入 server 指针，构造函数
 *     固定名 "/YomkLogger" 生效）→ LOGGER API 恢复且全链路可用
 * S1. YOMK_SHUTDOWN 全链路统一收尾（压轴）：EVENTLOOP_POST 积压、FILE_LOG_INFO 缓冲、
 *     异步 monitor 投递三类在途状态一次性排空——monitor 计数守恒、文件落盘（FileLogger
 *     析构 write）、API 回归 eInvalid + "YomkServer is not init"（API 守卫级：shutdown 内
 *     setServer(nullptr)，serverSnapshot 空守卫；服务级 eNo 仅出现在服务表内服务缺失时）、
 *     二次 shutdown 幂等
 *
 * 执行顺序约束：YomkAPI::init 为 static once_flag + call_once（单进程仅首次生效，shutdown 后
 *       再 init 返回空快照不可逆），故 shutdown 段必须压轴，重入/删除恢复段在其前完成
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
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
#include "YomkAPI.h"

namespace fs = std::filesystem;

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

// StringArray 响应解包（非 eOk/解包失败返回空表）
static std::vector<std::string> unpackStringArray(const YomkResponse& resp)
{
    std::vector<std::string> names;
    if (resp.m_status != YomkResponse::eOk || !resp.m_data)
    {
        return names;
    }
    auto arr = std::dynamic_pointer_cast<yomk::StringArray_>(resp.m_data);
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

int main()
{
    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT(1) 返回非空服务器");

    const int K = 20;

    // ============ S3. asyncRequest 回调内重入（异步 monitor 回调同步重入 /YomkContext/set） ============
    std::cout << "===== S3. asyncRequest 回调内重入 =====" << std::endl;
    {
        CHECK(YOMK_CONTEXT_ON_MONITOR().m_status == YomkResponse::eOk, "开启 monitor 开关");
        CHECK(YOMK_CONTEXT_CREATE("reentry_key", YomkMkPtr(String, std::string("k0"))).m_status == YomkResponse::eOk,
              "创建 reentry_key 成功");
        CHECK(YOMK_CONTEXT_CREATE("reentry_out", YomkMkPtr(String, std::string("out_init"))).m_status == YomkResponse::eOk,
              "创建 reentry_out 成功");

        g_reentryAsyncCalls.store(0);
        g_reentrySyncCalls.store(0);

        // 异步 monitor：监控池线程内同步重入 /YomkContext/set（无捕获 lambda + 全局原子计数）
        YOMK_CONTEXT_SET_MONITOR(
            "reentry_key",
            [](const yomk::Context&)
            {
                g_reentryAsyncCalls.fetch_add(1, std::memory_order_relaxed);
                YOMK_CONTEXT_SET("reentry_out", YomkMkPtr(String, std::string("out_") + std::to_string(g_reentryAsyncCalls.load())));
            },
            /*async=*/true);
        // 同步 monitor：重入 SET 的内联观察点（每次重入 SET 恰好触发一次）
        YOMK_CONTEXT_SET_MONITOR(
            "reentry_out",
            [](const yomk::Context&)
            {
                g_reentrySyncCalls.fetch_add(1, std::memory_order_relaxed);
            });

        bool allSetOk = true;
        for (int i = 0; i < K; ++i)
        {
            if (YOMK_CONTEXT_SET("reentry_key", YomkMkPtr(String, std::string("k") + std::to_string(i))).m_status !=
                YomkResponse::eOk)
            {
                allSetOk = false;
            }
        }
        CHECK(allSetOk, "K 次投递 SET 全部 eOk");

        CHECK(waitUntil([&]() { return g_reentrySyncCalls.load() == K; }, 5000),
              "重入链路在限时内收敛（不死锁，测试自然完成即证）");
        CHECK(g_reentryAsyncCalls.load() == K, "异步 monitor 计数守恒 == K");
        CHECK(g_reentrySyncCalls.load() == K, "重入 SET 触发同步 monitor 计数守恒 == K");

        auto last = YOMK_CONTEXT_GET(String, "reentry_out", YomkMkPtr(String, std::string("")));
        CHECK(last != nullptr && last->d == "out_" + std::to_string(K),
              "重入写入末值可读（异步 monitor 按提交序保序，末次重入为 out_K）");

        bool cleanupOk = YOMK_CONTEXT_OFF_MONITOR().m_status == YomkResponse::eOk &&
                         YOMK_CONTEXT_DESTROY("reentry_key").m_status == YomkResponse::eOk &&
                         YOMK_CONTEXT_DESTROY("reentry_out").m_status == YomkResponse::eOk;
        CHECK(cleanupOk, "S3 清场：OFF_MONITOR 与两键 DESTROY 全部 eOk");
    }

    // ============ S2. 删除内置服务契约与恢复（/YomkLogger，对齐 P0 S6 手法） ============
    std::cout << "===== S2. 删除内置服务契约与恢复（/YomkLogger） =====" << std::endl;
    {
        CHECK(YOMK_DEL_SERVICE("/YomkLogger") == 0, "删除内置 /YomkLogger 返回 0");
        auto afterDel = YOMK_LOGGER_INFO_LOGGERS("console");
        CHECK(afterDel.m_status == YomkResponse::eNo && afterDel.m_msg == "service not found: /YomkLogger",
              "删除后 LOGGER API 服务级 eNo + 精确消息");
        CHECK(!contains(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), "/YomkLogger"),
              "服务表快照不再含 /YomkLogger");

        CHECK(YomkAPI::newService<YomkLogger>() == 0, "newService 无参重注册返回 0（构造函数固定名生效）");
        auto restored = YOMK_LOGGER_INFO_LOGGERS("console");
        // loggers 按类型查询，行内容即 logger 纯名（YomkLogger::loggers 不再拼接类型后缀）
        CHECK(restored.m_status == YomkResponse::eOk && contains(unpackStringArray(restored), "MainLogger"),
              "重注册后 LOGGER API 恢复且含 MainLogger");
        CHECK(YomkAPI::CONSOLE_LOG_INFO_TAG("MainLogger", "[integration]", "s2 relink marker").m_status ==
                  YomkResponse::eOk,
              "重注册后日志 API 全链路可用（console_log eOk）");
    }

    // ============ S1. YOMK_SHUTDOWN 全链路统一收尾（压轴：call_once 后不可再 init） ============
    std::cout << "===== S1. YOMK_SHUTDOWN 全链路统一收尾（压轴） =====" << std::endl;
    {
        fs::path tmpDir = fs::temp_directory_path() / ("yomk_integration_s1_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::create_directories(tmpDir, ec);
        CHECK(!ec, "S1 临时目录创建成功: " + tmpDir.string());

        // 事件积压（不等待执行完成；积压处理语义不保证全执行，仅验证 shutdown 有限时间返回）
        CHECK(YOMK_EVENTLOOP_START("integration_loop").m_status == YomkResponse::eOk, "启动 integration_loop");
        bool postOk = true;
        for (int i = 0; i < 100; ++i)
        {
            if (YOMK_EVENTLOOP_POST("integration_loop", nullptr,
                                    [](YomkPkgPtr) { return YomkResponse(YomkResponse::eOk); })
                     .m_status != YomkResponse::eOk)
            {
                postOk = false;
            }
        }
        CHECK(postOk, "100 个积压事件全部 POST eOk");

        // 文件日志：FILE_LOG_INFO 仅入缓冲，FileLogger 析构 write 落盘
        // （FILE_LOG_CREATE 第二参为日志器名，实际文件 = <dir>/<名>.log，由 FileLogger 自动追加后缀）
        CHECK(YOMK_FILE_LOG_CREATE(tmpDir.string(), "integration_file").m_status == YomkResponse::eOk,
              "FILE_LOG_CREATE 返回 eOk");
        CHECK(YOMK_FILE_INFO("integration_file", "s1_shutdown_flush_marker").m_status == YomkResponse::eOk,
              "FILE_LOG_INFO 入缓冲返回 eOk（日志器名与 CREATE 一致）");

        // 异步 monitor 投递（shutdown 排空断言主体；S3 清场时全局开关已关，此处需重新开启）
        CHECK(YOMK_CONTEXT_ON_MONITOR().m_status == YomkResponse::eOk, "重新开启 monitor 开关");
        CHECK(YOMK_CONTEXT_CREATE("drain_key", YomkMkPtr(String, std::string("d0"))).m_status == YomkResponse::eOk,
              "创建 drain_key 成功");
        g_drainCalls.store(0);
        YOMK_CONTEXT_SET_MONITOR(
            "drain_key", [](const yomk::Context&) { g_drainCalls.fetch_add(1, std::memory_order_relaxed); },
            /*async=*/true);
        const int N = 50;
        bool drainSetOk = true;
        for (int i = 0; i < N; ++i)
        {
            if (YOMK_CONTEXT_SET("drain_key", YomkMkPtr(String, std::string("d") + std::to_string(i))).m_status !=
                YomkResponse::eOk)
            {
                drainSetOk = false;
            }
        }
        CHECK(drainSetOk, "N 次异步通知投递全部 eOk");

        // 统一收尾：排空异步 monitor + 落盘文件日志 + 清空服务表
        YOMK_SHUTDOWN();

        CHECK(g_drainCalls.load() == N, "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到无丢失）");
        std::string fileContent;
        bool readOk = readFile(tmpDir / "integration_file.log", fileContent);
        CHECK(readOk && fileContent.find("s1_shutdown_flush_marker") != std::string::npos,
              "shutdown 返回后文件日志已落盘且含标记串（析构 write）");

        // API 守卫级回归：serverSnapshot 已置空 → eInvalid + "YomkServer is not init"
        auto postLoop = YOMK_EVENTLOOP_POST("integration_loop", nullptr, YomkServiceFunc());
        CHECK(postLoop.m_status == YomkResponse::eInvalid && postLoop.m_msg == "YomkServer is not init",
              "shutdown 后 EVENTLOOP_POST 回归 eInvalid + 守卫消息");
        auto postCtx = YOMK_CONTEXT_SET("drain_key", YomkMkPtr(String, std::string("x")));
        CHECK(postCtx.m_status == YomkResponse::eInvalid && postCtx.m_msg == "YomkServer is not init",
              "shutdown 后 CONTEXT_SET 回归 eInvalid + 守卫消息");
        auto postLog = YOMK_LOGGER_INFO_ALL();
        CHECK(postLog.m_status == YomkResponse::eInvalid && postLog.m_msg == "YomkServer is not init",
              "shutdown 后 LOGGER API 回归 eInvalid + 守卫消息");
        auto postInfo = YOMK_SERVER_INFO_ALL();
        CHECK(postInfo.m_status == YomkResponse::eInvalid && postInfo.m_msg == "YomkServer is not init",
              "shutdown 后 SERVER_INFO API 回归 eInvalid + 守卫消息");

        YOMK_SHUTDOWN();  // 幂等：二次调用空转安全
        CHECK(true, "shutdown 二次调用幂等无崩溃");

        fs::remove_all(tmpDir, ec);
    }

    std::cout << (g_failed == 0 ? "TestYomkIntegration all check passed." : "TestYomkIntegration FAILED")
              << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
