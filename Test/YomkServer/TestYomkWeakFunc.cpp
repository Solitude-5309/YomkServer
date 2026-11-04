/**
 * @file TestYomkWeakFunc.cpp
 * @brief YomkService::weakFunc 弱绑定生命周期语义测试（正式全面测试，MC3）
 *
 * 覆盖内容：
 * 1. 存活路径：服务存活时四种返回值回调均真实执行（weakFunc lambda 与 weakFunc 成员函数指针绑定两种方式）
 * 2. 销毁丢弃（引用计数路径）：服务析构后回调丢弃——void 不执行、yomk::Response 返回 ResStatus::Failed+契约消息、
 *    CheckStatus 默认放行 CheckStatus::Accept、其他类型返回 Ret{} 默认值
 * 3. 删除即停（注销标志路径）：持 shared_ptr 不释放经框架删除置位注销标志，回调同样立即丢弃（双层判活标志层独立生效）
 * 4. installFunc 自动弱绑定串联：注销后 invoke 返回丢弃契约消息（真实功能函数路径）
 * 5. 构造期告警分支：构造期内调用 weakFunc（weak_from_this 未生效），绑定永久失效、回调永不执行
 * 6. 丢弃不执行实证：所有丢弃用例均以副作用计数未变化断言，而非仅看返回值
 *
 * 说明：不依赖单例与服务器生命周期，YomkServer::create 直接持有，
 *       服务由测试手工 shared_ptr 控制销毁时机（避开 call_once 约束，单进程即可覆盖全部场景）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

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

// 弱绑定丢弃契约消息片段（四种返回值分支共用）
static const std::string DISCARD_MSG = "deleted or unregistered";

/**
 * @brief 弱绑定演示服务：供存活/销毁/注销场景绑定回调
 */
class BindSrv : public YomkService
{
public:
    BindSrv(YomkServer* server) : YomkService(server) { name("/BindSrv"); }

    // 白盒再暴露：weakFunc 生命周期语义（存活/丢弃/注销分支）只能持有回调句柄验证，
    // invoke 契约消息断言需直调命中安装函数——均无 URL 通道可达；断言与被测语义零变化
    using YomkService::weakFunc;
    using YomkService::invoke;

public:
    virtual int init() override
    {
        // installFunc 自动弱绑定：注销后经 invoke 返回丢弃契约消息
        installFunc("/installed", &BindSrv::workResp);
        return 0;
    }

public:
    // weakFunc 成员函数指针绑定真实执行计数
    std::atomic<int> memberCount{0};

    yomk::Response workResp(YomkPkgPtr pkg) { return {yomk::ResStatus::Succeeded, "alive"}; }
    yomk::Response memberWork(YomkPkgPtr pkg)
    {
        memberCount.fetch_add(1);
        return {yomk::ResStatus::Succeeded, "member"};
    }

    // weakFunc 成员函数指针绑定（外流注册主路径）
    std::function<yomk::Response(YomkPkgPtr)> makeMemberCb() { return weakFunc(&BindSrv::memberWork); }
};

/**
 * @brief 构造期绑定演示服务：构造函数内调用 weakFunc（weak_from_this 未生效，告警分支载体）
 */
class CtorBindSrv : public YomkService
{
public:
    std::atomic<int> count{0};
    std::function<yomk::Response(YomkPkgPtr)> ctorCb;

    CtorBindSrv(YomkServer* server) : YomkService(server)
    {
        // 构造期 weak_from_this() 未生效：框架告警，绑定永久失效
        ctorCb = weakFunc(
            [this](YomkPkgPtr pkg) -> yomk::Response
            {
                count.fetch_add(1);
                return {yomk::ResStatus::Succeeded, "ctor"};
            });
    }

public:
    virtual int init() override { return 0; }
};

// 1. 存活路径：四种返回值回调与宏绑定回调均真实执行
static void testAliveCallbacks(YomkServer* server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};
    std::atomic<int> checkCount{0};
    std::atomic<int> intCount{0};

    auto srv = std::make_shared<BindSrv>(server);
    std::function<void()> cbVoid = srv->weakFunc([&voidCount]() { voidCount.fetch_add(1); });
    std::function<yomk::Response(YomkPkgPtr)> cbResp = srv->weakFunc(
        [&respCount](YomkPkgPtr pkg)
        {
            respCount.fetch_add(1);
            return yomk::Response{yomk::ResStatus::Succeeded, "alive"};
        });
    std::function<yomk::CheckStatus(const yomk::Context&)> cbCheck = srv->weakFunc(
        [&checkCount](const yomk::Context& ctx)
        {
            checkCount.fetch_add(1);
            return yomk::CheckStatus::Reject;
        });
    std::function<int(YomkPkgPtr)> cbInt = srv->weakFunc(
        [&intCount](YomkPkgPtr pkg)
        {
            intCount.fetch_add(1);
            return 42;
        });
    std::function<yomk::Response(YomkPkgPtr)> cbMember = srv->makeMemberCb();

    cbVoid();
    yomk::Context ctx{"key", nullptr};
    yomk::Response resp = cbResp(nullptr);
    yomk::CheckStatus status = cbCheck(ctx);
    int intRet = cbInt(nullptr);
    yomk::Response memberResp = cbMember(nullptr);

    YOMK_TEST_CHECK(voidCount.load() == 1, "存活：void 回调真实执行");
    YOMK_TEST_CHECK(
        respCount.load() == 1 && resp.m_status == yomk::ResStatus::Succeeded && resp.m_msg == "alive",
        "存活：yomk::Response 回调正常执行");
    YOMK_TEST_CHECK(checkCount.load() == 1 && status == yomk::CheckStatus::Reject, "存活：CheckStatus 回调返回真实值");
    YOMK_TEST_CHECK(intCount.load() == 1 && intRet == 42, "存活：其他返回值类型回调返回真实值");
    YOMK_TEST_CHECK(
        srv->memberCount.load() == 1 && memberResp.m_status == yomk::ResStatus::Succeeded && memberResp.m_msg == "member",
        "存活：weakFunc 成员函数指针绑定回调真实执行");
}

// 2. 销毁丢弃（引用计数路径）：服务析构后四类回调全部丢弃
static void testDestroyDiscard(YomkServer* server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};
    std::atomic<int> checkCount{0};
    std::atomic<int> intCount{0};

    std::function<void()> cbVoid;
    std::function<yomk::Response(YomkPkgPtr)> cbResp;
    std::function<yomk::CheckStatus(const yomk::Context&)> cbCheck;
    std::function<int(YomkPkgPtr)> cbInt;
    std::function<yomk::Response(YomkPkgPtr)> cbMember;
    {
        auto srv = std::make_shared<BindSrv>(server);
        cbVoid = srv->weakFunc([&voidCount]() { voidCount.fetch_add(1); });
        cbResp = srv->weakFunc(
            [&respCount](YomkPkgPtr pkg)
            {
                respCount.fetch_add(1);
                return yomk::Response{yomk::ResStatus::Succeeded, "alive"};
            });
        cbCheck = srv->weakFunc(
            [&checkCount](const yomk::Context& ctx)
            {
                checkCount.fetch_add(1);
                return yomk::CheckStatus::Reject;
            });
        cbInt = srv->weakFunc(
            [&intCount](YomkPkgPtr pkg)
            {
                intCount.fetch_add(1);
                return 42;
            });
        cbMember = srv->makeMemberCb();
    }  // srv 在此析构：引用计数归零，弱绑定全部失效

    cbVoid();
    yomk::Context ctx{"key", nullptr};
    yomk::Response resp = cbResp(nullptr);
    yomk::CheckStatus status = cbCheck(ctx);
    int intRet = cbInt(nullptr);
    yomk::Response memberResp = cbMember(nullptr);

    YOMK_TEST_CHECK(voidCount.load() == 0, "销毁：void 回调丢弃，副作用计数未变化");
    YOMK_TEST_CHECK(
        respCount.load() == 0 && resp.m_status == yomk::ResStatus::Failed &&
            resp.m_msg.find(DISCARD_MSG) != std::string::npos,
        "销毁：yomk::Response 回调丢弃，返回 ResStatus::Failed + 契约消息");
    YOMK_TEST_CHECK(
        checkCount.load() == 0 && status == yomk::CheckStatus::Accept,
        "销毁：CheckStatus 回调丢弃，默认放行 CheckStatus::Accept");
    YOMK_TEST_CHECK(intCount.load() == 0 && intRet == 0, "销毁：其他返回值类型回调丢弃，返回 Ret{} 默认值");
    YOMK_TEST_CHECK(
        memberResp.m_status == yomk::ResStatus::Failed && memberResp.m_msg.find(DISCARD_MSG) != std::string::npos,
        "销毁：weakFunc 成员函数指针绑定回调丢弃");
}

// 3. 删除即停（注销标志路径）：持引用不释放经框架删除置位注销标志，回调与自动弱绑定 invoke 立即丢弃
static void testMarkDeletedDiscard(YomkServer* server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};

    auto* rawSrv = new BindSrv(server);
    YOMK_TEST_CHECK(server->addService(rawSrv) == 0, "注册成功（框架 init 安装 /installed）");
    // 经 shared_from_this 与框架共享所有权：删除后本副本使服务对象仍存活
    std::shared_ptr<YomkService> srv = rawSrv->shared_from_this();

    // 注销前：自动弱绑定 invoke 命中执行（经 BindSrv 白盒再暴露；srv 基类副本仅承担生命周期）
    yomk::Response aliveResp = rawSrv->invoke("/installed");
    YOMK_TEST_CHECK(
        aliveResp.m_status == yomk::ResStatus::Succeeded && aliveResp.m_msg == "alive",
        "注销前：installFunc 自动弱绑定 invoke 命中执行");

    std::function<void()> cbVoid = rawSrv->weakFunc([&voidCount]() { voidCount.fetch_add(1); });
    std::function<yomk::Response(YomkPkgPtr)> cbResp = rawSrv->weakFunc(
        [&respCount](YomkPkgPtr pkg)
        {
            respCount.fetch_add(1);
            return yomk::Response{yomk::ResStatus::Succeeded, "alive"};
        });

    // 持 shared_ptr 不释放，经框架删除置位注销标志（双层判活标志层独立于引用计数）
    YOMK_TEST_CHECK(server->delService("/BindSrv") == 0, "框架删除成功");

    cbVoid();
    yomk::Response resp = cbResp(nullptr);
    yomk::Response invokeResp = rawSrv->invoke("/installed");

    YOMK_TEST_CHECK(voidCount.load() == 0, "删除即停：void 回调立即丢弃，副作用计数未变化");
    YOMK_TEST_CHECK(
        respCount.load() == 0 && resp.m_status == yomk::ResStatus::Failed &&
            resp.m_msg.find(DISCARD_MSG) != std::string::npos,
        "删除即停：yomk::Response 回调立即丢弃（删除即停）");
    YOMK_TEST_CHECK(
        invokeResp.m_status == yomk::ResStatus::Failed && invokeResp.m_msg.find(DISCARD_MSG) != std::string::npos,
        "删除即停：自动弱绑定 invoke 返回丢弃契约消息");
}

// 4. 构造期告警分支：构造期捕获的绑定永久失效，回调永不执行
static void testConstructorWeakFunc(YomkServer* server)
{
    auto srv = std::make_shared<CtorBindSrv>(server);  // 构造期 weakFunc 触发框架告警
    yomk::Response resp = srv->ctorCb(nullptr);

    YOMK_TEST_CHECK(srv->count.load() == 0, "构造期告警：回调从未执行，副作用计数为零");
    YOMK_TEST_CHECK(
        resp.m_status == yomk::ResStatus::Failed && resp.m_msg.find(DISCARD_MSG) != std::string::npos,
        "构造期告警：绑定永久失效，持有后调用仍返回丢弃契约消息");
}

int main()
{
    auto server = YomkServer::create(1);

    testAliveCallbacks(server.get());
    testDestroyDiscard(server.get());
    testMarkDeletedDiscard(server.get());
    testConstructorWeakFunc(server.get());

    if (g_failed > 0)
    {
        std::cout << "TestYomkWeakFunc failed, count: " << g_failed << std::endl;
        return 1;
    }
    std::cout << "TestYomkWeakFunc all check passed." << std::endl;
    return 0;
}
