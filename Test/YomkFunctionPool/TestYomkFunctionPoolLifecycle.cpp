/**
 * @file TestYomkFunctionPoolLifecycle.cpp
 * @brief FunctionPool 模块生命周期与 CRUD 基线测试（FPC1，从零新建——模块此前零专属覆盖）
 *
 * 覆盖内容（8 个 Section，约 55 断言）：
 * 1. CRUD 闭环：REGISTER echo 函数 → CALL 解包回传消息 → UNREGISTER → CALL not-found
 * 2. 注册边界：空函数名 / null func 均 eInvalid；重复注册为"更新"语义（CALL 走新函数）
 * 3. 注销边界：空名 eInvalid / 未注册名 eInvalid（现状契约，P1 登记项）
 * 4. 调用边界：空名 eInvalid / 未注册 eInvalid（P1）/ CallFunction.m_pkg=nullptr 原样透传给函数
 * 5. 内省：INFO_NAMES 存活列表包含注册名（空表陷阱教训）/ INFO_NAME 命中 "名 [类型]" 与
 *    无类型两种格式、未注册 eNo、空名 eInvalid / INFO_ALL "functions:N" 首行相对计数与行格式
 * 6. 宏分发：YOMK_FUNCTIONPOOL_REGISTER 2 参（无类型）与 3 参（#MsgName 字符串化为类型名）
 * 7. 异常契约（P2 现状固化）：CALL 链（request→invoke→callFunction→用户函数）无任何 try/catch，
 *    用户函数抛 std::exception 异常穿透直达调用方——测试侧捕获 what 验证；随后正常 CALL 验证池存活
 * 8. 超长名：65536 字符函数名全生命周期（REGISTER/CALL/INFO_NAME/UNREGISTER）eOk 且完整回显
 *
 * 说明：YomkFunctionPool 是 YomkService 子类（构造绑定 server->weak_from_this()），无法脱离
 *       YOMK_INIT 单例白盒直连；全部经 API 宏测试，同时覆盖 invoke 路由链，无覆盖损失。
 *       测试装置为文件级原子计数（TSan-clean 既有模式）。FPC1 源码零改动：P1（not-found 错误码
 *       unregister/call=eInvalid vs funcInfo=eNo 不一致）与 P2（异常穿透）仅按现状固化断言并登记，
 *       处置留 FPC2。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

// ---- 文件级观测装置（TSan-clean 既有模式）----
static std::atomic<long> g_nullPkgSeen{0};

// echo：回传入参 pkg（验证 CALL 消息透传与解包）
static YomkResponse echoFunc(YomkPkgPtr pkg)
{
    return {YomkResponse::eOk, "echo", pkg};
}

// probe：记录是否收到 nullptr 入参（验证 m_pkg=nullptr 透传）
static YomkResponse nullProbeFunc(YomkPkgPtr pkg)
{
    if (pkg == nullptr)
    {
        g_nullPkgSeen.fetch_add(1);
    }
    return {YomkResponse::eOk, "probe"};
}

// 版本函数：区分重复注册更新前后的实现
static YomkResponse v1Func(YomkPkgPtr)
{
    return {YomkResponse::eOk, "v1"};
}

static YomkResponse v2Func(YomkPkgPtr)
{
    return {YomkResponse::eOk, "v2"};
}

// 抛异常函数：验证 CALL 链异常穿透契约（P2）
static YomkResponse throwFunc(YomkPkgPtr)
{
    throw std::runtime_error("fp_boom");
}

// 正常存活函数
static YomkResponse aliveFunc(YomkPkgPtr)
{
    return {YomkResponse::eOk, "alive"};
}

// 从 INFO_ALL 首行 "functions:N" 解析当前函数总数；失败返回 SIZE_MAX
static size_t poolCount()
{
    auto r = YOMK_FUNCTIONPOOL_INFO_ALL();
    YomkUnPackPkg(r.m_data, StringArray, arr);
    if (!arr || arr->d.empty())
    {
        return SIZE_MAX;
    }
    const std::string &first = arr->d.front();
    if (first.rfind("functions:", 0) != 0)
    {
        return SIZE_MAX;
    }
    try
    {
        return static_cast<size_t>(std::stoul(first.substr(10)));
    }
    catch (...)
    {
        return SIZE_MAX;
    }
}

int main()
{
    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    // ============ Section 1: CRUD 闭环 ============
    {
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_echo", echoFunc).m_status == YomkResponse::eOk,
              "REGISTER 注册函数返回 eOk");
        auto resp = YOMK_FUNCTIONPOOL_CALL("fp_echo", YomkMkPtr(String, std::string("hello_fp")));
        CHECK(resp.m_status == YomkResponse::eOk, "CALL 已注册函数返回 eOk");
        YomkUnPackPkg(resp.m_data, String, echoMsg);
        CHECK(echoMsg != nullptr && echoMsg->d == "hello_fp", "CALL 回传消息解包一致（echo 语义）");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_echo").m_status == YomkResponse::eOk,
              "UNREGISTER 注销函数返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_echo", YomkMkPtr(String, std::string("x"))).m_status == YomkResponse::eInvalid,
              "注销后 CALL 返回 eInvalid（现状契约 P1）");
    }

    // ============ Section 2: 注册边界与重复注册更新语义 ============
    {
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("", echoFunc).m_status == YomkResponse::eInvalid,
              "REGISTER 空函数名返回 eInvalid");
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_null", nullptr).m_status == YomkResponse::eInvalid,
              "REGISTER null 函数返回 eInvalid");

        // 重复注册 = 更新：CALL 走新实现
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_upd", v1Func).m_status == YomkResponse::eOk,
              "REGISTER v1 返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_upd", nullptr).m_msg.find("v1") != std::string::npos,
              "CALL 走 v1 实现");
        auto upd = YOMK_FUNCTIONPOOL_REGISTER("fp_upd", v2Func);
        CHECK(upd.m_status == YomkResponse::eOk, "重复 REGISTER 返回 eOk（更新语义）");
        CHECK(upd.m_msg.find("update") != std::string::npos, "重复 REGISTER 消息说明更新语义");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_upd", nullptr).m_msg.find("v2") != std::string::npos,
              "更新后 CALL 走 v2 实现");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_upd").m_status == YomkResponse::eOk, "清理 fp_upd");
    }

    // ============ Section 3: 注销边界 ============
    {
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("").m_status == YomkResponse::eInvalid,
              "UNREGISTER 空函数名返回 eInvalid");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_never_registered").m_status == YomkResponse::eInvalid,
              "UNREGISTER 未注册名返回 eInvalid（现状契约 P1）");
    }

    // ============ Section 4: 调用边界与 m_pkg=nullptr 透传 ============
    {
        CHECK(YOMK_FUNCTIONPOOL_CALL("", YomkMkPtr(String, std::string("x"))).m_status == YomkResponse::eInvalid,
              "CALL 空函数名返回 eInvalid");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_never_registered", YomkMkPtr(String, std::string("x"))).m_status == YomkResponse::eInvalid,
              "CALL 未注册名返回 eInvalid（现状契约 P1）");

        g_nullPkgSeen.store(0);
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_probe", nullProbeFunc).m_status == YomkResponse::eOk,
              "REGISTER 探针函数返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_probe", nullptr).m_status == YomkResponse::eOk,
              "CALL m_pkg=nullptr 正常执行");
        CHECK(g_nullPkgSeen.load() == 1, "函数收到 nullptr 入参（m_pkg 原样透传）");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_probe").m_status == YomkResponse::eOk, "清理 fp_probe");
    }

    // ============ Section 5: 内省三接口 ============
    {
        // 准备：一个无类型、一个有类型（经 3 参宏注册，见 S6 前置）
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_plain", aliveFunc).m_status == YomkResponse::eOk,
              "REGISTER 无类型函数 fp_plain");

        // INFO_NAMES：存活列表包含已注册名（空表陷阱教训：必须有存活条目时调用）
        auto namesResp = YOMK_FUNCTIONPOOL_INFO_NAMES();
        CHECK(namesResp.m_status == YomkResponse::eOk, "INFO_NAMES 返回 eOk");
        YomkUnPackPkg(namesResp.m_data, StringArray, namesArr);
        bool namesFound = false;
        if (namesArr)
        {
            for (const auto &n : namesArr->d)
            {
                if (n == "fp_plain")
                {
                    namesFound = true;
                }
            }
        }
        CHECK(namesArr != nullptr && namesFound, "INFO_NAMES 列表包含 fp_plain");

        // INFO_NAME：无类型格式（纯名）与未注册/空名
        auto infoResp = YOMK_FUNCTIONPOOL_INFO_NAME("fp_plain");
        CHECK(infoResp.m_status == YomkResponse::eOk, "INFO_NAME 已注册名返回 eOk");
        CHECK(infoResp.m_msg == "fp_plain", "INFO_NAME 无类型函数消息为纯名（无括号后缀）");
        CHECK(YOMK_FUNCTIONPOOL_INFO_NAME("fp_never_registered").m_status == YomkResponse::eNo,
              "INFO_NAME 未注册名返回 eNo（与 unregister/call 的 eInvalid 不一致——P1 登记项）");
        CHECK(YOMK_FUNCTIONPOOL_INFO_NAME("").m_status == YomkResponse::eInvalid,
              "INFO_NAME 空函数名返回 eInvalid");

        // INFO_ALL：首行 functions:N 相对计数（新注册一个 → 计数 +1）与行格式
        size_t before = poolCount();
        CHECK(before != SIZE_MAX, "INFO_ALL 首行 functions:N 可解析");
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_count_probe", aliveFunc).m_status == YomkResponse::eOk,
              "REGISTER 计数探针函数");
        size_t after = poolCount();
        CHECK(after == before + 1, "INFO_ALL 计数随注册 +1（共享池相对计数）");
        auto allResp = YOMK_FUNCTIONPOOL_INFO_ALL();
        YomkUnPackPkg(allResp.m_data, StringArray, allArr);
        bool allLineFound = false;
        if (allArr)
        {
            for (const auto &l : allArr->d)
            {
                if (l == "fp_plain")
                {
                    allLineFound = true;
                }
            }
        }
        CHECK(allArr != nullptr && allLineFound, "INFO_ALL 列表行包含 fp_plain（纯名格式）");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_count_probe").m_status == YomkResponse::eOk, "清理 fp_count_probe");
    }

    // ============ Section 6: REGISTER 宏 2 参 / 3 参分发 ============
    {
        // 2 参：无类型声明
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_two", aliveFunc).m_status == YomkResponse::eOk,
              "REGISTER 2 参宏（无类型）返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_INFO_NAME("fp_two").m_msg == "fp_two",
              "2 参注册 INFO_NAME 为纯名");

        // 3 参：#MsgName 字符串化为类型名，INFO_NAME 显示 [类型]
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_three", echoFunc, String).m_status == YomkResponse::eOk,
              "REGISTER 3 参宏（String 类型）返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_INFO_NAME("fp_three").m_msg == "fp_three [String]",
              "3 参注册 INFO_NAME 显示 [String] 类型后缀");

        // 清理
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_two").m_status == YomkResponse::eOk, "清理 fp_two");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_three").m_status == YomkResponse::eOk, "清理 fp_three");
    }

    // ============ Section 7: CALL 异常穿透契约（P2 现状固化）============
    {
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_throw", throwFunc).m_status == YomkResponse::eOk,
              "REGISTER 抛异常函数返回 eOk");

        // CALL 链五层（request→invoke→callFunction→用户函数）无 try/catch：
        // 异常穿透直达调用方（区别于 EventLoop run() 的吞噬设计）
        bool caught = false;
        try
        {
            YOMK_FUNCTIONPOOL_CALL("fp_throw", nullptr);
        }
        catch (const std::runtime_error &e)
        {
            caught = (std::string(e.what()) == "fp_boom");
        }
        CHECK(caught, "用户函数异常穿透 CALL 链直达调用方（what 一致）——P2 现状契约");

        // 穿透后池存活：正常注册与调用不受影响
        CHECK(YOMK_FUNCTIONPOOL_REGISTER("fp_alive", aliveFunc).m_status == YomkResponse::eOk,
              "异常穿透后 REGISTER 仍正常");
        CHECK(YOMK_FUNCTIONPOOL_CALL("fp_alive", nullptr).m_msg.find("alive") != std::string::npos,
              "异常穿透后 CALL 正常函数执行");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_throw").m_status == YomkResponse::eOk, "清理 fp_throw");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_alive").m_status == YomkResponse::eOk, "清理 fp_alive");
    }

    // ============ Section 8: 超长函数名边界 ============
    {
        std::string longName(65536, 'F');
        CHECK(YOMK_FUNCTIONPOOL_REGISTER(longName, aliveFunc).m_status == YomkResponse::eOk,
              "REGISTER 65536 字节函数名返回 eOk");
        CHECK(YOMK_FUNCTIONPOOL_CALL(longName, nullptr).m_status == YomkResponse::eOk,
              "CALL 超长函数名返回 eOk");
        auto longInfo = YOMK_FUNCTIONPOOL_INFO_NAME(longName);
        CHECK(longInfo.m_status == YomkResponse::eOk, "INFO_NAME 超长函数名返回 eOk");
        CHECK(longInfo.m_msg.find(longName) != std::string::npos, "INFO_NAME 完整回显超长函数名");
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER(longName).m_status == YomkResponse::eOk,
              "UNREGISTER 超长函数名返回 eOk");
    }

    // ============ 收尾清理（不污染后续闭环/其他测试的共享池）============
    {
        CHECK(YOMK_FUNCTIONPOOL_UNREGISTER("fp_plain").m_status == YomkResponse::eOk, "清理 fp_plain");
    }

    YOMK_SHUTDOWN();

    if (g_failed == 0)
    {
        std::cout << "TestYomkFunctionPoolLifecycle all check passed." << std::endl;
        return 0;
    }
    std::cout << "TestYomkFunctionPoolLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
    return 1;
}
