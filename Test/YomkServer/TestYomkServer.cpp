/**
 * @file TestYomkServer.cpp
 * @brief YomkServer URL 解析路由与服务增删改查白盒测试（正式全面测试，MC4）
 *
 * 覆盖内容：
 * 1. parseRequestUrl 全边界（经公开 request 路径）：空串/无 / 开头/无第二 /、多段路径语义、
 *    合法命中与 pkg 透传端到端、超长 URL；"srv is empty"与"funcName is empty"两分支不可达
 *    （url[0]=='/' 且 posEnd>=1 保证 srvName 至少含 /、funcName 以 / 开头必非空），书面豁免
 * 2. asyncRequest 解析边界：非法 url 回调永不触发；合法 url 回调到达；func 为空分支
 * 3. addService 守卫与回滚：nullptr、init 返回 -1、init 抛 bad_alloc/runtime_error（修复后
 *    异常视同失败统一回滚，无半初始化残留、进程不终止）
 * 4. 同名替换：旧服务先 markDeleted 后 deinit、新服务后 init、表内唯一；
 *    替换+新服务 init 失败→旧不恢复、表为空（语义文档化）
 * 5. delService：不存在 -1、删除即停端到端（全局弱绑定回调丢弃，兑现 MC3 预埋）、重复删除 -1
 * 6. 内省：serviceNames 随增删变化、serviceFuncInfos 命中与元数据、不存在服务返回空表
 * 7. newService 模板：有名注册可路由；无名以空名注册（现状文档化：URL 不可达，不修复仅记录）
 * 8. startService：空表、不支持名 continue、内置 /YomkServerInfo creator 真实路径
 *
 * 说明：全程不触碰 shutdown/YOMK_INIT（服务器析构兜底），每节独立 YomkServer::create 实例，
 *       节首重置全局观测变量；排空/幂等/拒新语义归 MC5
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
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

// ---- 文件级观测变量：init/deinit 次序与状态、注入开关（均为单线程节内使用） ----
static std::atomic<int> g_initCount{0};                  // init 调用次数
static std::atomic<int> g_deinitCount{0};                // deinit 调用次数
static std::atomic<bool> g_deletedOnDeinit{false};       // deinit 时刻 deleted() 状态
static std::atomic<int> g_pingCount{0};                  // /ping 真实执行次数
static int g_initRet = 0;                                // init 返回值注入：0 成功 / -1 失败
static int g_initThrow = 0;                              // init 异常注入：0 不抛 / 1 bad_alloc / 2 runtime_error
static std::function<YomkResponse(YomkPkgPtr)> g_weakCb; // init 时存下的弱绑定回调（删除即停端到端）

static void resetGlobals()
{
    g_initCount.store(0);
    g_deinitCount.store(0);
    g_deletedOnDeinit.store(false);
    g_pingCount.store(0);
    g_initRet = 0;
    g_initThrow = 0;
    g_weakCb = nullptr;
}

static bool nameIn(const std::vector<std::string> &names, const std::string &srvName)
{
    for (auto &n : names)
    {
        if (n == srvName)
        {
            return true;
        }
    }
    return false;
}

static int nameCount(const std::vector<std::string> &names, const std::string &srvName)
{
    int count = 0;
    for (auto &n : names)
    {
        if (n == srvName)
        {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 路由演示服务：/ping 固定响应、/echo 解包回显；init/deinit 记录观测变量
 */
class RouteSrv : public YomkService
{
public:
    RouteSrv(YomkServer *server)
        : YomkService(server)
    {
    }

public:
    virtual int init() override
    {
        g_initCount.fetch_add(1);
        if (g_initThrow == 1)
        {
            throw std::bad_alloc(); // N9：init 抛 bad_alloc（修复后应被捕获回滚）
        }
        if (g_initThrow == 2)
        {
            throw std::runtime_error("init failed on purpose");
        }
        if (g_initRet != 0)
        {
            return g_initRet; // 既有回滚路径：init 返回非 0
        }
        YomkInstallFunc("/ping", RouteSrv::ping, String);
        YomkInstallFunc("/echo", RouteSrv::echo, String);
        // 存弱绑定回调至全局：服务删除后仍可从外部调用，验证删除即停端到端
        g_weakCb = YomkBindWeakSelf(RouteSrv::ping);
        return 0;
    }

    virtual void deinit() override
    {
        g_deinitCount.fetch_add(1);
        g_deletedOnDeinit.store(deleted()); // markDeleted 应先于 deinit（承诺实证）
    }

private:
    YomkResponse ping(YomkPkgPtr pkg)
    {
        g_pingCount.fetch_add(1);
        return {YomkResponse::eOk, "pong"};
    }
    YomkResponse echo(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, String, data);
        return {YomkResponse::eOk, "echo:" + data->d};
    }
};

// 1. URL 解析全边界与路由（经公开 request 路径）
static void testUrlParseBoundaries()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<RouteSrv>("/RouteSrv") == 0, "注册路由演示服务成功");

    // 空串 / 无 / 开头：please start with / 契约
    YomkResponse resp = server->request("");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("please start with /") != std::string::npos,
          "request 空 url 返回 eNo + please start with /");
    resp = server->request("no_slash/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("please start with /") != std::string::npos,
          "request 无 / 开头 url 返回 eNo + please start with /");

    // 无第二 /：not found service name 契约
    resp = server->request("/");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("not found service name") != std::string::npos,
          "request \"/\" 返回 eNo + not found service name");
    resp = server->request("/Only");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("not found service name") != std::string::npos,
          "request \"/Only\"（无第二 /）返回 eNo + not found service name");

    // 多段路径：srvName="/RouteSrv"、funcName="/b/c" 未安装 → not found（语义现状）
    resp = server->request("/RouteSrv/b/c");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("not found") != std::string::npos,
          "request 多段路径 funcName=\"/b/c\" 未安装返回 eNo + not found");

    // 服务未注册：service not found 契约
    resp = server->request("/NoSuch/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("service not found") != std::string::npos,
          "request 未注册服务返回 eNo + service not found");

    // 合法命中 + pkg 透传端到端
    resp = server->request("/RouteSrv/ping");
    CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "pong", "request 合法 url 命中返回 eOk + pong");
    resp = server->request("/RouteSrv/echo", YomkMkPtr(String, std::string("hi")));
    CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "echo:hi",
          "request 携 pkg 路由透传，解包回显往返正确");

    // 超长 URL（4096 级服务名）：不崩溃，走 service not found
    resp = server->request("/" + std::string(4096, 'x') + "/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("service not found") != std::string::npos,
          "request 超长 url 不崩溃，返回 eNo + service not found");
}

// 2. asyncRequest 解析边界与到达（排空/关闭语义归 MC5）
static void testAsyncRequestParse()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<RouteSrv>("/RouteSrv") == 0, "异步用例注册路由演示服务成功");

    // 非法 url：回调永不触发（解析失败直接丢弃）
    std::atomic<int> badCbCount{0};
    server->asyncRequest("bad_url", nullptr, [&badCbCount](YomkResponse resp)
                         { badCbCount.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(badCbCount.load() == 0, "asyncRequest 非法 url：回调永不触发");

    // 合法 url：回调到达并收到 eOk（promise + 超时保护）
    std::promise<YomkResponse> prom;
    auto fut = prom.get_future();
    server->asyncRequest("/RouteSrv/ping", nullptr, [&prom](YomkResponse resp)
                         { prom.set_value(resp); });
    bool ready = fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    CHECK(ready, "asyncRequest 合法 url：回调在超时窗口内到达");
    if (ready)
    {
        YomkResponse resp = fut.get();
        CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "pong",
              "asyncRequest 回调收到 eOk + pong");
    }

    // func 为空分支：请求仍被执行（fire-and-forget）
    int pingBefore = g_pingCount.load();
    server->asyncRequest("/RouteSrv/ping");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(g_pingCount.load() > pingBefore, "asyncRequest func 为空：请求仍被执行");
}

// 3. addService 守卫与回滚（含 init 异常回滚修复验证，N9）
static void testAddServiceGuardAndRollback()
{
    auto server = YomkServer::create(1);
    resetGlobals();

    CHECK(server->addService(nullptr) == -1, "addService(nullptr) 返回 -1");

    // init 返回 -1：既有回滚路径
    g_initRet = -1;
    auto *failRetSrv = new RouteSrv(server.get());
    failRetSrv->name("/FailRet");
    CHECK(server->addService(failRetSrv) == -1, "init 返回 -1：addService 返回 -1");
    CHECK(!nameIn(server->serviceNames(), "/FailRet"), "init 返回 -1：回滚后服务表无残留");
    CHECK(g_deinitCount.load() == 1, "init 返回 -1：回滚路径触发 deinit");

    // init 抛 bad_alloc：修复后异常视同失败，统一回滚
    g_initRet = 0;
    g_initThrow = 1;
    auto *badAllocSrv = new RouteSrv(server.get());
    badAllocSrv->name("/ThrowBadAlloc");
    CHECK(server->addService(badAllocSrv) == -1, "init 抛 bad_alloc：addService 返回 -1（异常不穿透，进程不终止）");
    CHECK(!nameIn(server->serviceNames(), "/ThrowBadAlloc"), "init 抛 bad_alloc：无半初始化残留");

    // init 抛 runtime_error：同上
    g_initThrow = 2;
    auto *runtimeSrv = new RouteSrv(server.get());
    runtimeSrv->name("/ThrowRuntime");
    CHECK(server->addService(runtimeSrv) == -1, "init 抛 runtime_error：addService 返回 -1（异常不穿透）");
    CHECK(!nameIn(server->serviceNames(), "/ThrowRuntime"), "init 抛 runtime_error：无半初始化残留");
    g_initThrow = 0;

    CHECK(g_deinitCount.load() == 3, "三次失败注册均触发回滚 deinit（累计 3 次）");
}

// 4. 同名替换：先删后立承诺 + 替换失败语义文档化
static void testReplaceSameName()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    CHECK(server->newService<RouteSrv>("/Dup") == 0, "同名替换：首次注册成功");

    int initBefore = g_initCount.load();
    CHECK(server->newService<RouteSrv>("/Dup") == 0, "同名第二实例注册（替换）返回 0");
    CHECK(g_initCount.load() == initBefore + 1, "替换：新服务 init 在旧服务清理后被调用");
    CHECK(g_deinitCount.load() == 1, "替换：旧服务 deinit 被调用");
    CHECK(g_deletedOnDeinit.load() == true, "替换：旧服务 deinit 时 deleted() 已为 true（先 markDeleted 承诺实证）");

    YomkResponse resp = server->request("/Dup/ping");
    CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "pong", "替换：新服务可路由");
    CHECK(nameCount(server->serviceNames(), "/Dup") == 1, "替换：serviceNames 中该名仅一条");

    // 替换 + 新服务 init 失败：旧已删、新回滚、表为空（语义文档化：不恢复旧服务）
    g_initRet = -1;
    auto *badSrv = new RouteSrv(server.get());
    badSrv->name("/Dup");
    CHECK(server->addService(badSrv) == -1, "替换 + 新服务 init 失败：返回 -1");
    CHECK(!nameIn(server->serviceNames(), "/Dup"), "替换失败后该名为空（旧服务不恢复，语义文档化）");
    resp = server->request("/Dup/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("service not found") != std::string::npos,
          "替换失败后请求返回 eNo + service not found");
    g_initRet = 0;
}

// 5. delService：返回值契约 + 删除即停端到端（兑现 MC3 预埋）
static void testDelService()
{
    auto server = YomkServer::create(1);
    resetGlobals();

    CHECK(server->delService("/NotExist") == -1, "delService 不存在服务返回 -1");
    CHECK(server->newService<RouteSrv>("/Del") == 0, "删除用例注册服务成功");
    CHECK(g_weakCb != nullptr, "init 时已存下全局弱绑定回调");

    CHECK(server->delService("/Del") == 0, "delService 已存在服务返回 0");
    CHECK(g_deinitCount.load() == 1, "delService 触发 deinit（锁外清理）");
    CHECK(g_deletedOnDeinit.load() == true, "delService：markDeleted 先于 deinit");

    YomkResponse resp = server->request("/Del/ping");
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("service not found") != std::string::npos,
          "删除后请求返回 eNo + service not found");
    CHECK(!nameIn(server->serviceNames(), "/Del"), "删除后 serviceNames 不含该名");
    CHECK(server->delService("/Del") == -1, "重复删除返回 -1");

    // 删除即停端到端：服务已被框架析构，全局弱绑定回调丢弃
    resp = g_weakCb(nullptr);
    CHECK(resp.m_status == YomkResponse::eNo && resp.m_msg.find("deleted or unregistered") != std::string::npos,
          "delService 后弱绑定回调丢弃（删除即停端到端，兑现 MC3 预埋）");
    g_weakCb = nullptr;
}

// 6. 内省接口：serviceNames / serviceFuncInfos
static void testIntrospection()
{
    auto server = YomkServer::create(1);
    resetGlobals();
    server->newService<RouteSrv>("/RouteSrv");

    CHECK(nameIn(server->serviceNames(), "/RouteSrv"), "serviceNames 反映注册");

    auto infos = server->serviceFuncInfos("/RouteSrv");
    CHECK(infos.count("/ping") == 1 && infos.count("/echo") == 1, "serviceFuncInfos 命中 /ping 与 /echo");
    CHECK(infos["/ping"].m_funcName == "/ping" && infos["/ping"].m_msgName == "String" &&
              infos["/echo"].m_msgName == "String",
          "serviceFuncInfos 元数据正确（m_funcName/m_msgName）");
    CHECK(server->serviceFuncInfos("/NoSuch").empty(), "serviceFuncInfos 不存在服务返回空表");

    server->delService("/RouteSrv");
    CHECK(!nameIn(server->serviceNames(), "/RouteSrv"), "serviceNames 反映删除");
}

// 7. newService 模板：有名注册 + 无名空名注册（现状文档化）
static void testNewServiceTemplate()
{
    auto server = YomkServer::create(1);
    resetGlobals();

    CHECK(server->newService<RouteSrv>("/Tpl") == 0, "newService 有名注册返回 0");
    CHECK(server->request("/Tpl/ping").m_status == YomkResponse::eOk, "newService 有名注册可路由");

    // 无名：以空名注册。URL 解析保证 srvName 至少含 /，空名服务永不可达——现状文档化，不修复仅记录
    CHECK(server->newService<RouteSrv>() == 0, "newService 无名返回 0（以空名注册，现状文档化）");
    CHECK(nameIn(server->serviceNames(), ""), "serviceNames 含空串（空名服务 URL 不可达，现状文档化）");
}

// 8. startService：空表 / 不支持名 continue / 内置 creator 真实路径
static void testStartService()
{
    auto server = YomkServer::create(1);
    resetGlobals();

    CHECK(server->startService({}) == 0, "startService 空表返回 0");
    CHECK(server->startService({"/NoSuchBuiltin"}) == 0, "startService 不支持名返回 0（continue 分支）");
    CHECK(!nameIn(server->serviceNames(), "/NoSuchBuiltin"), "startService 不支持名不入表");
    CHECK(server->startService({"/YomkServerInfo"}) == 0, "startService 内置服务返回 0");
    CHECK(nameIn(server->serviceNames(), "/YomkServerInfo"), "内置 creator 真实路径：/YomkServerInfo 入表");
}

int main()
{
    testUrlParseBoundaries();
    testAsyncRequestParse();
    testAddServiceGuardAndRollback();
    testReplaceSameName();
    testDelService();
    testIntrospection();
    testNewServiceTemplate();
    testStartService();

    if (g_failed > 0)
    {
        std::cout << "TestYomkServer failed, count: " << g_failed << std::endl;
        return 1;
    }
    std::cout << "TestYomkServer all check passed." << std::endl;
    return 0;
}
