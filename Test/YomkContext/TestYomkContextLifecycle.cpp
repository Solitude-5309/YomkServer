/**
 * @file TestYomkContextLifecycle.cpp
 * @brief YomkContext 服务生命周期与异常安全白盒测试（Context 模块 MC3）
 *
 * 覆盖内容：
 * 1. init 效果（内省）：内置 /YomkContext 注册、init 装配 14 个功能函数
 * 2. 基线运行：异步 monitor 池正常工作
 * 3. delService 触发 deinit + 排空：删除前投递的异步 monitor 全部排空执行
 * 4. 删除后不可路由 + 重复删除契约：请求返回 service not found、重复删除返回 -1
 * 5. 重新注册 + monitor 池重建：newService<YomkContext> 后 init 重新装配、池重建可用
 * 6. 异常安全：派生服务 init() 抛 std::bad_alloc，addService try/catch 回滚无残留
 *
 * 说明：全程使用 YomkAPI::init() 单例拉起内置 /YomkContext；需构造/派生内部类 YomkContext，
 *       故 include 非导出内部头 Modules/Context/YomkContext.h（CMake 已追加 src 目录）；
 *       末尾调用 YomkAPI::shutdown() 干净 deinit 重注册实例（call_once 约束，shutdown 后不可再初始化）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "Modules/Context/YomkContext.h"
#include "YomkAPI.h"

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

// 派生自内部类 YomkContext，init() 抛 std::bad_alloc 以验证 addService 的 try/catch 回滚路径
// （模拟 YomkContext::init 内 make_unique<YomkSimpleThreadPool> 分配失败）
class ThrowContext : public YomkContext
{
public:
    using YomkContext::YomkContext;
    int init() override { throw std::bad_alloc(); }
};

// ---- 文件级观测变量 ----
static std::atomic<int> g_asyncCalls{0};  // 异步 monitor 被调用次数

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

// 判断服务名列表中是否包含指定名字
static bool hasService(const std::vector<std::string>& names, const std::string& target)
{
    return std::find(names.begin(), names.end(), target) != names.end();
}

int main()
{
    auto server = YomkAPI::init(1);
    YOMK_TEST_CHECK(server != nullptr, "YomkAPI::init() 返回非空服务器");

    // ============ Section 1: init 效果（内省）============
    {
        auto names = YomkAPI::serverInstance()->serviceNames();
        YOMK_TEST_CHECK(hasService(names, "/YomkContext"), "内置 /YomkContext 已注册");

        auto infos = YomkAPI::serverInstance()->serviceFuncInfos("/YomkContext");
        YOMK_TEST_CHECK(infos.size() == 14, "init 装配 14 个功能函数");
    }

    // ============ Section 2: 基线运行（异步 monitor 池工作）============
    {
        YomkAPI::CONTEXT_ON_MONITOR();
        auto cr = YomkAPI::CONTEXT_CREATE("life_base", yomkmsg::String::shared(std::string("b0")));
        YOMK_TEST_CHECK(cr.m_status == YomkResponse::eOk, "创建 life_base 成功");

        g_asyncCalls.store(0);
        YomkAPI::CONTEXT_SET_MONITOR(
            "life_base", [](const yomk::Context&) { ++g_asyncCalls; }, /*async=*/true);

        YomkAPI::CONTEXT_SET("life_base", yomkmsg::String::shared(std::string("b1")));
        YomkAPI::CONTEXT_SET("life_base", yomkmsg::String::shared(std::string("b2")));

        bool reached = waitForCount(g_asyncCalls, 2, 2000);
        YOMK_TEST_CHECK(reached && g_asyncCalls.load() == 2, "基线：异步 monitor 池正常工作（计数 == 2）");
        YomkAPI::CONTEXT_OFF_MONITOR();
    }

    // ============ Section 3: delService 触发 deinit + 排空 ============
    {
        YomkAPI::CONTEXT_ON_MONITOR();
        auto cr = YomkAPI::CONTEXT_CREATE("life_drain", yomkmsg::String::shared(std::string("d0")));
        YOMK_TEST_CHECK(cr.m_status == YomkResponse::eOk, "创建 life_drain 成功");

        g_asyncCalls.store(0);
        YomkAPI::CONTEXT_SET_MONITOR(
            "life_drain", [](const yomk::Context&) { ++g_asyncCalls; }, /*async=*/true);

        const int N = 50;
        for (int i = 0; i < N; ++i)
        {
            YomkAPI::CONTEXT_SET("life_drain", yomkmsg::String::shared(std::string("d") + std::to_string(i)));
        }

        // 立即删除服务：触发 markDeleted + deinit 排空 monitor 池
        int delRet = YomkAPI::delService("/YomkContext");
        YOMK_TEST_CHECK(delRet == 0, "delService(/YomkContext) 返回 0");

        // delService 返回后排空已完成，计数应等于投递次数（无丢失、无迟到）
        YOMK_TEST_CHECK(g_asyncCalls.load() == N, "delService 排空后异步 monitor 计数 == 投递次数");
    }

    // ============ Section 4: 删除后不可路由 + 重复删除契约 ============
    {
        auto resp = YomkAPI::request("/YomkContext/keys", nullptr);
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eNo, "删除后请求 /YomkContext 返回 eNo");
        YOMK_TEST_CHECK(resp.m_msg == "service not found: /YomkContext", "删除后消息为 service not found: /YomkContext");

        auto names = YomkAPI::serverInstance()->serviceNames();
        YOMK_TEST_CHECK(!hasService(names, "/YomkContext"), "serviceNames 不再包含 /YomkContext");

        int delAgain = YomkAPI::delService("/YomkContext");
        YOMK_TEST_CHECK(delAgain == -1, "重复删除不存在服务返回 -1");
    }

    // ============ Section 5: 重新注册 + monitor 池重建 ============
    {
        int reAdd = YomkAPI::newService<YomkContext>("/YomkContext");
        YOMK_TEST_CHECK(reAdd == 0, "重新注册 YomkContext 返回 0");

        auto infos = YomkAPI::serverInstance()->serviceFuncInfos("/YomkContext");
        YOMK_TEST_CHECK(infos.size() == 14, "重新注册后 init 重新装配 14 个功能函数");

        YomkAPI::CONTEXT_ON_MONITOR();
        auto cr = YomkAPI::CONTEXT_CREATE("life_rebuild", yomkmsg::String::shared(std::string("r0")));
        YOMK_TEST_CHECK(cr.m_status == YomkResponse::eOk, "重新注册后创建 life_rebuild 成功");

        g_asyncCalls.store(0);
        YomkAPI::CONTEXT_SET_MONITOR(
            "life_rebuild", [](const yomk::Context&) { ++g_asyncCalls; }, /*async=*/true);

        YomkAPI::CONTEXT_SET("life_rebuild", yomkmsg::String::shared(std::string("r1")));
        YomkAPI::CONTEXT_SET("life_rebuild", yomkmsg::String::shared(std::string("r2")));
        YomkAPI::CONTEXT_SET("life_rebuild", yomkmsg::String::shared(std::string("r3")));

        bool reached = waitForCount(g_asyncCalls, 3, 2000);
        YOMK_TEST_CHECK(reached && g_asyncCalls.load() == 3, "重新注册后 monitor 池重建可用（计数 == 3）");
        YomkAPI::CONTEXT_OFF_MONITOR();
    }

    // ============ Section 6: 异常安全（init 抛 bad_alloc 回滚）============
    {
        int addRet = YomkAPI::addService(new ThrowContext(YomkAPI::serverInstance().get()), "/ThrowContext");
        YOMK_TEST_CHECK(addRet == -1, "init 抛 bad_alloc 时 addService 返回 -1");

        auto names = YomkAPI::serverInstance()->serviceNames();
        YOMK_TEST_CHECK(!hasService(names, "/ThrowContext"), "回滚后 serviceNames 不含 /ThrowContext（无残留）");

        // 进程存活、/YomkContext 仍可用
        auto resp = YomkAPI::CONTEXT_INFO_KEYS();
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eOk, "异常回滚后 /YomkContext 仍可用");
    }

    YomkAPI::shutdown();

    if (g_failed == 0)
    {
        std::cout << "TestYomkContextLifecycle all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkContextLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
