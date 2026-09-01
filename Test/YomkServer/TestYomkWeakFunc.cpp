/**
 * @file TestYomkWeakFunc.cpp
 * @brief YomkService::weakFunc 弱绑定生命周期语义测试（正式全面测试，MC3）
 *
 * 覆盖内容：
 * 1. 存活路径：服务存活时四种返回值回调均真实执行（weakFunc lambda 与 YomkBindWeakSelf 成员绑定两种方式）
 * 2. 销毁丢弃（引用计数路径）：服务析构后回调丢弃——void 不执行、YomkResponse 返回 eNo+契约消息、
 *    ECheckStatus 默认放行 eAccept、其他类型返回 Ret{} 默认值
 * 3. 删除即停（注销标志路径）：持 shared_ptr 不释放仅 markDeleted，回调同样立即丢弃（双层判活标志层独立生效）
 * 4. YomkInstallFunc 自动弱绑定串联：注销后 invoke 返回丢弃契约消息（真实功能函数路径）
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

// 弱绑定丢弃契约消息片段（四种返回值分支共用）
static const std::string DISCARD_MSG = "deleted or unregistered";

/**
 * @brief 弱绑定演示服务：供存活/销毁/注销场景绑定回调
 */
class BindSrv : public YomkService
{
public:
    BindSrv(YomkServer *server)
        : YomkService(server)
    {
        name("/BindSrv");
    }

public:
    virtual int init() override
    {
        // YomkInstallFunc 自动弱绑定：注销后经 invoke 返回丢弃契约消息
        YomkInstallFunc("/installed", BindSrv::workResp);
        return 0;
    }

public:
    // YomkBindWeakSelf 成员绑定真实执行计数
    std::atomic<int> macroCount{0};

    YomkResponse workResp(YomkPkgPtr pkg)
    {
        return {YomkResponse::eOk, "alive"};
    }
    YomkResponse macroWork(YomkPkgPtr pkg)
    {
        macroCount.fetch_add(1);
        return {YomkResponse::eOk, "macro"};
    }

    // YomkBindWeakSelf 宏绑定成员函数（外流注册主路径）
    std::function<YomkResponse(YomkPkgPtr)> makeMacroCb()
    {
        return YomkBindWeakSelf(BindSrv::macroWork);
    }
};

/**
 * @brief 构造期绑定演示服务：构造函数内调用 weakFunc（weak_from_this 未生效，告警分支载体）
 */
class CtorBindSrv : public YomkService
{
public:
    std::atomic<int> count{0};
    std::function<YomkResponse(YomkPkgPtr)> ctorCb;

    CtorBindSrv(YomkServer *server)
        : YomkService(server)
    {
        // 构造期 weak_from_this() 未生效：框架告警，绑定永久失效
        ctorCb = weakFunc([this](YomkPkgPtr pkg) -> YomkResponse
                          {
            count.fetch_add(1);
            return {YomkResponse::eOk, "ctor"}; });
    }

public:
    virtual int init() override
    {
        return 0;
    }
};

// 1. 存活路径：四种返回值回调与宏绑定回调均真实执行
static void testAliveCallbacks(YomkServer *server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};
    std::atomic<int> checkCount{0};
    std::atomic<int> intCount{0};

    auto srv = std::make_shared<BindSrv>(server);
    std::function<void()> cbVoid = srv->weakFunc([&voidCount]()
                                                 { voidCount.fetch_add(1); });
    std::function<YomkResponse(YomkPkgPtr)> cbResp = srv->weakFunc([&respCount](YomkPkgPtr pkg)
                                                                   {
        respCount.fetch_add(1);
        return YomkResponse{YomkResponse::eOk, "alive"}; });
    std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context &)> cbCheck =
        srv->weakFunc([&checkCount](const yomk::Context &ctx)
                      {
        checkCount.fetch_add(1);
        return yomk::ContextChecker::eReject; });
    std::function<int(YomkPkgPtr)> cbInt = srv->weakFunc([&intCount](YomkPkgPtr pkg)
                                                         {
        intCount.fetch_add(1);
        return 42; });
    std::function<YomkResponse(YomkPkgPtr)> cbMacro = srv->makeMacroCb();

    cbVoid();
    yomk::Context ctx{"key", nullptr};
    YomkResponse resp = cbResp(nullptr);
    yomk::ContextChecker::ECheckStatus status = cbCheck(ctx);
    int intRet = cbInt(nullptr);
    YomkResponse macroResp = cbMacro(nullptr);

    CHECK(voidCount.load() == 1, "存活：void 回调真实执行");
    CHECK(respCount.load() == 1 && resp.m_status == YomkResponse::eOk && resp.m_msg == "alive",
          "存活：YomkResponse 回调正常执行");
    CHECK(checkCount.load() == 1 && status == yomk::ContextChecker::eReject,
          "存活：ECheckStatus 回调返回真实值");
    CHECK(intCount.load() == 1 && intRet == 42, "存活：其他返回值类型回调返回真实值");
    CHECK(srv->macroCount.load() == 1 && macroResp.m_status == YomkResponse::eOk && macroResp.m_msg == "macro",
          "存活：YomkBindWeakSelf 成员绑定回调真实执行");
}

// 2. 销毁丢弃（引用计数路径）：服务析构后四类回调全部丢弃
static void testDestroyDiscard(YomkServer *server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};
    std::atomic<int> checkCount{0};
    std::atomic<int> intCount{0};

    std::function<void()> cbVoid;
    std::function<YomkResponse(YomkPkgPtr)> cbResp;
    std::function<yomk::ContextChecker::ECheckStatus(const yomk::Context &)> cbCheck;
    std::function<int(YomkPkgPtr)> cbInt;
    std::function<YomkResponse(YomkPkgPtr)> cbMacro;
    {
        auto srv = std::make_shared<BindSrv>(server);
        cbVoid = srv->weakFunc([&voidCount]()
                               { voidCount.fetch_add(1); });
        cbResp = srv->weakFunc([&respCount](YomkPkgPtr pkg)
                               {
            respCount.fetch_add(1);
            return YomkResponse{YomkResponse::eOk, "alive"}; });
        cbCheck = srv->weakFunc([&checkCount](const yomk::Context &ctx)
                                {
            checkCount.fetch_add(1);
            return yomk::ContextChecker::eReject; });
        cbInt = srv->weakFunc([&intCount](YomkPkgPtr pkg)
                              {
            intCount.fetch_add(1);
            return 42; });
        cbMacro = srv->makeMacroCb();
    } // srv 在此析构：引用计数归零，弱绑定全部失效

    cbVoid();
    yomk::Context ctx{"key", nullptr};
    YomkResponse resp = cbResp(nullptr);
    yomk::ContextChecker::ECheckStatus status = cbCheck(ctx);
    int intRet = cbInt(nullptr);
    YomkResponse macroResp = cbMacro(nullptr);

    CHECK(voidCount.load() == 0, "销毁：void 回调丢弃，副作用计数未变化");
    CHECK(respCount.load() == 0 && resp.m_status == YomkResponse::eNo &&
              resp.m_msg.find(DISCARD_MSG) != std::string::npos,
          "销毁：YomkResponse 回调丢弃，返回 eNo + 契约消息");
    CHECK(checkCount.load() == 0 && status == yomk::ContextChecker::eAccept,
          "销毁：ECheckStatus 回调丢弃，默认放行 eAccept");
    CHECK(intCount.load() == 0 && intRet == 0, "销毁：其他返回值类型回调丢弃，返回 Ret{} 默认值");
    CHECK(macroResp.m_status == YomkResponse::eNo && macroResp.m_msg.find(DISCARD_MSG) != std::string::npos,
          "销毁：YomkBindWeakSelf 绑定回调丢弃");
}

// 3. 删除即停（注销标志路径）：持引用不释放仅 markDeleted，回调与自动弱绑定 invoke 立即丢弃
static void testMarkDeletedDiscard(YomkServer *server)
{
    std::atomic<int> voidCount{0};
    std::atomic<int> respCount{0};

    auto srv = std::make_shared<BindSrv>(server);
    srv->init(); // 安装 /installed（YomkInstallFunc 自动弱绑定）

    // 注销前：自动弱绑定 invoke 命中执行
    YomkResponse aliveResp = srv->invoke("/installed");
    CHECK(aliveResp.m_status == YomkResponse::eOk && aliveResp.m_msg == "alive",
          "注销前：YomkInstallFunc 自动弱绑定 invoke 命中执行");

    std::function<void()> cbVoid = srv->weakFunc([&voidCount]()
                                                 { voidCount.fetch_add(1); });
    std::function<YomkResponse(YomkPkgPtr)> cbResp = srv->weakFunc([&respCount](YomkPkgPtr pkg)
                                                                   {
        respCount.fetch_add(1);
        return YomkResponse{YomkResponse::eOk, "alive"}; });

    srv->markDeleted(); // 持 shared_ptr 不释放，仅置位注销标志（双层判活标志层）

    cbVoid();
    YomkResponse resp = cbResp(nullptr);
    YomkResponse invokeResp = srv->invoke("/installed");

    CHECK(voidCount.load() == 0, "markDeleted：void 回调立即丢弃，副作用计数未变化");
    CHECK(respCount.load() == 0 && resp.m_status == YomkResponse::eNo &&
              resp.m_msg.find(DISCARD_MSG) != std::string::npos,
          "markDeleted：YomkResponse 回调立即丢弃（删除即停）");
    CHECK(invokeResp.m_status == YomkResponse::eNo && invokeResp.m_msg.find(DISCARD_MSG) != std::string::npos,
          "markDeleted：自动弱绑定 invoke 返回丢弃契约消息");
}

// 4. 构造期告警分支：构造期捕获的绑定永久失效，回调永不执行
static void testConstructorWeakFunc(YomkServer *server)
{
    auto srv = std::make_shared<CtorBindSrv>(server); // 构造期 weakFunc 触发框架告警
    YomkResponse resp = srv->ctorCb(nullptr);

    CHECK(srv->count.load() == 0, "构造期告警：回调从未执行，副作用计数为零");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find(DISCARD_MSG) != std::string::npos,
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
