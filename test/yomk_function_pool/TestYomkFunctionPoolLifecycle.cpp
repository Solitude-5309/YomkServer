// FunctionPool 模块生命周期与 CRUD 基线测试
//
// 覆盖内容（8 个 Section，约 55 断言）：
// 1. CRUD 闭环：install() echo 函数 → call() 解包回传消息 → uninstall() → call() not-found
// 2. 注册边界：空函数名 / null func 均 ResStatus::Invalid；重复注册为"更新"语义（call() 走新函数）
// 3. 注销边界：空名 ResStatus::Invalid / 未注册名 ResStatus::Invalid（现状契约）
// 4. 调用边界：空名 ResStatus::Invalid / 未注册 ResStatus::Invalid/ CallFunction.m_pkg=nullptr 原样透传给函数
// 5. 内省：list() 存活列表包含注册名（空表陷阱教训）/ get_info() 命中 "名 [类型]" 与
// 无类型两种格式、未注册 ResStatus::Failed、空名 ResStatus::Invalid / all() FuncInfoList 相对计数与字段断言
// 6. 注册形态：fn_name+fn 两步（无类型）/ +msg_name 三步（声明期望消息类型名）/
// +description 四步（追加函数说明，get_info() m_msg 契约保持 + 数据段可读）与同名热替换覆盖语义
// 7. 异常契约：call() 链（request→invoke→callFunction→用户函数）无任何 try/catch，
// 用户函数抛 std::exception 异常穿透直达调用方——测试侧捕获 what 验证；随后正常 call() 验证池存活
// 8. 超长名：65536 字符函数名全生命周期（install()/call()/get_info()/uninstall()）ResStatus::Succeeded 且完整回显
//
// 说明：YomkFunctionPool 是 YomkService 子类（构造绑定 server->weak_from_this()），无法脱离
// yomk::server().init() 单例白盒直连；全部经 API 接口测试，同时覆盖 invoke 路由链，无覆盖损失。
// 测试装置为文件级原子计数（TSan-clean 既有模式）。契约：not-found 统一返回
// ResStatus::Failed（与 Context/EventLoop/路由层惯例一致）；call() 异常穿透直达调用方，
// 为显式契约（S7 固化）。
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "yomk/fn_pool/YomkFunctionPoolApi.h"

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

// ---- 文件级观测装置（TSan-clean 既有模式）----
static std::atomic<long> g_nullPkgSeen{0};

// echo：回传入参 pkg（验证 call() 消息透传与解包）
static yomk::payload::Response echoFunc(YomkMsgPtr pkg)
{
    return {yomk::payload::ResStatus::Succeeded, "echo", pkg};
}

// probe：记录是否收到 nullptr 入参（验证 m_pkg=nullptr 透传）
static yomk::payload::Response nullProbeFunc(YomkMsgPtr pkg)
{
    if (pkg == nullptr)
    {
        g_nullPkgSeen.fetch_add(1);
    }
    return {yomk::payload::ResStatus::Succeeded, "probe"};
}

// 版本函数：区分重复注册更新前后的实现
static yomk::payload::Response v1Func(YomkMsgPtr)
{
    return {yomk::payload::ResStatus::Succeeded, "v1"};
}

static yomk::payload::Response v2Func(YomkMsgPtr)
{
    return {yomk::payload::ResStatus::Succeeded, "v2"};
}

// 抛异常函数：验证 call() 链异常穿透契约
static yomk::payload::Response throwFunc(YomkMsgPtr)
{
    throw std::runtime_error("fp_boom");
}

// 正常存活函数
static yomk::payload::Response aliveFunc(YomkMsgPtr)
{
    return {yomk::payload::ResStatus::Succeeded, "alive"};
}

// 从 all() 解包 FuncInfoList 取当前函数总数（d.size() 承载）；失败返回 SIZE_MAX
static size_t poolCount()
{
    auto r = yomk::fn_pool().all();
    auto arr = yomk::msgs::unpack<yomk::msgs::FuncInfoList>(r.m_data);
    if (!arr)
    {
        return SIZE_MAX;
    }
    return arr->d.size();
}

int main()
{
    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

    // ============ Section 1: CRUD 闭环 ============
    {
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_echo").fn(echoFunc).install() == yomk::payload::ResStatus::Succeeded, "install() 注册函数返回 ResStatus::Succeeded");
        auto resp = yomk::fn_pool().fn_name("fp_echo").msg(yomk::msgs::String::shared(std::string("hello_fp"))).call();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "call() 已注册函数返回 ResStatus::Succeeded");
        auto echoMsg = yomk::msgs::unpack<yomk::msgs::String>(resp.m_data);
        YOMK_TEST_CHECK(echoMsg != nullptr && echoMsg->d == "hello_fp", "call() 回传消息解包一致（echo 语义）");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_echo").uninstall() == yomk::payload::ResStatus::Succeeded, "uninstall() 注销函数返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_echo").msg(yomk::msgs::String::shared(std::string("x"))).call().m_status == yomk::payload::ResStatus::Failed,
            "注销后 call() 返回 ResStatus::Failed（FPC2 契约：not-found 统一 ResStatus::Failed）");
    }

    // ============ Section 2: 注册边界与重复注册更新语义 ============
    {
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("").fn(echoFunc).install() == yomk::payload::ResStatus::Invalid,
            "install() 空函数名返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_null").fn(nullptr).install() == yomk::payload::ResStatus::Invalid,
            "install() null 函数返回 ResStatus::Invalid");

        // 重复注册 = 更新：call() 走新实现
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_upd").fn(v1Func).install() == yomk::payload::ResStatus::Succeeded, "install() v1 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_upd").msg(nullptr).call().m_msg.find("v1") != std::string::npos, "call() 走 v1 实现");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_upd").fn(v2Func).install() == yomk::payload::ResStatus::Succeeded,
              "重复 install() 返回 ResStatus::Succeeded（更新语义）");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_upd").msg(nullptr).call().m_msg.find("v2") != std::string::npos, "更新后 call() 走 v2 实现");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_upd").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_upd");
    }

    // ============ Section 3: 注销边界 ============
    {
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("").uninstall() == yomk::payload::ResStatus::Invalid, "uninstall() 空函数名返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_never_registered").uninstall() == yomk::payload::ResStatus::Failed,
            "uninstall() 未注册名返回 ResStatus::Failed（FPC2 契约：not-found 统一 ResStatus::Failed）");
    }

    // ============ Section 4: 调用边界与 m_pkg=nullptr 透传 ============
    {
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("").msg(yomk::msgs::String::shared(std::string("x"))).call().m_status == yomk::payload::ResStatus::Invalid,
            "call() 空函数名返回 ResStatus::Invalid");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_never_registered").msg(yomk::msgs::String::shared(std::string("x"))).call().m_status ==
                yomk::payload::ResStatus::Failed,
            "call() 未注册名返回 ResStatus::Failed（FPC2 契约：not-found 统一 ResStatus::Failed）");

        g_nullPkgSeen.store(0);
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_probe").fn(nullProbeFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 探针函数返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_probe").msg(nullptr).call().m_status == yomk::payload::ResStatus::Succeeded, "call() m_pkg=nullptr 正常执行");
        YOMK_TEST_CHECK(g_nullPkgSeen.load() == 1, "函数收到 nullptr 入参（m_pkg 原样透传）");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_probe").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_probe");
    }

    // ============ Section 5: 内省三接口 ============
    {
        // 准备：一个无类型、一个有类型（经 3 参注册，见 S6 前置）
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_plain").fn(aliveFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 无类型函数 fp_plain");

        // list()：存活列表包含已注册名（空表陷阱教训：必须有存活条目时调用）
        auto namesResp = yomk::fn_pool().list();
        YOMK_TEST_CHECK(namesResp.m_status == yomk::payload::ResStatus::Succeeded, "list() 返回 ResStatus::Succeeded");
        auto namesArr = yomk::msgs::unpack<yomk::msgs::StringArray>(namesResp.m_data);
        bool namesFound = false;
        if (namesArr)
        {
            for (const auto& n : namesArr->d)
            {
                if (n == "fp_plain")
                {
                    namesFound = true;
                }
            }
        }
        YOMK_TEST_CHECK(namesArr != nullptr && namesFound, "list() 列表包含 fp_plain");

        // get_info()：结构化返回（FuncInfo 包恒挂）与未注册/空名
        auto infoResp = yomk::fn_pool().fn_name("fp_plain").get_info();
        YOMK_TEST_CHECK(infoResp.m_status == yomk::payload::ResStatus::Succeeded, "get_info() 已注册名返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(infoResp.m_msg == "ok", "get_info() 命中 m_msg 恒为 ok");
        auto infoPlainResp = yomk::msgs::unpack<yomk::msgs::FuncInfo>(infoResp.m_data);
        YOMK_TEST_CHECK(infoPlainResp != nullptr, "get_info() 数据为 FuncInfo 包");
        YOMK_TEST_CHECK(infoPlainResp && infoPlainResp->d.m_funcName == "fp_plain", "get_info() 字段 m_funcName 一致");
        YOMK_TEST_CHECK(infoPlainResp && infoPlainResp->d.m_msgName.empty() && infoPlainResp->d.m_description.empty(),
              "2 参注册 m_msgName/m_description 均为空");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_never_registered").get_info().m_status == yomk::payload::ResStatus::Failed,
            "get_info() 未注册名返回 ResStatus::Failed（与 unregister/call 一致——FPC2 后全接口统一）");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("").get_info().m_status == yomk::payload::ResStatus::Invalid, "get_info() 空函数名返回 ResStatus::Invalid");

        // all()：结构化 FuncInfoList 相对计数（新注册一个 → size +1）与字段断言
        size_t before = poolCount();
        YOMK_TEST_CHECK(before != SIZE_MAX, "all() FuncInfoList 可解包");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_count_probe").fn(aliveFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 计数探针函数");
        size_t after = poolCount();
        YOMK_TEST_CHECK(after == before + 1, "all() 计数随注册 +1（共享池相对计数）");
        auto allResp = yomk::fn_pool().all();
        YOMK_TEST_CHECK(allResp.m_msg == "ok", "all() 成功消息恒为 ok");
        auto allArr = yomk::msgs::unpack<yomk::msgs::FuncInfoList>(allResp.m_data);
        bool allFound = false;
        if (allArr)
        {
            for (const auto& fi : allArr->d)
            {
                if (fi.m_funcName == "fp_plain")
                {
                    allFound = fi.m_msgName.empty() && fi.m_description.empty();
                }
            }
        }
        YOMK_TEST_CHECK(allArr != nullptr && allFound, "all() 列表包含 fp_plain（m_msgName/m_description 均空）");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_count_probe").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_count_probe");
    }

    // ============ Section 6: install() 2 参 / 3 参形态 ============
    {
        // 2 参：无类型声明
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_two").fn(aliveFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 2 参（无类型）返回 ResStatus::Succeeded");
        auto twoResp = yomk::fn_pool().fn_name("fp_two").get_info();
        auto twoInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(twoResp.m_data);
        YOMK_TEST_CHECK(twoResp.m_msg == "ok" && twoInfo && twoInfo->d.m_funcName == "fp_two" &&
                  twoInfo->d.m_msgName.empty() && twoInfo->d.m_description.empty(),
              "2 参注册 get_info() 结构化字段（纯名无类型无说明）");

        // 3 参：声明期望消息类型名，get_info() 显示 [类型]
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_three").fn(echoFunc).msg_name("String").install() == yomk::payload::ResStatus::Succeeded,
            "install() 3 参（String 类型）返回 ResStatus::Succeeded");
        auto threeResp = yomk::fn_pool().fn_name("fp_three").get_info();
        auto threeInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(threeResp.m_data);
        YOMK_TEST_CHECK(threeResp.m_msg == "ok" && threeInfo && threeInfo->d.m_funcName == "fp_three" &&
                  threeInfo->d.m_msgName == "String",
              "3 参注册 get_info() 字段 m_msgName == \"String\"");

        // 4 参：追加函数说明，get_info() m_msg 契约保持、说明挂数据段
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_desc").fn(echoFunc).msg_name("String").description("fp description").install() == yomk::payload::ResStatus::Succeeded,
              "install() 4 参（String 类型 + 说明）返回 ResStatus::Succeeded");
        auto infoDesc = yomk::fn_pool().fn_name("fp_desc").get_info();
        YOMK_TEST_CHECK(infoDesc.m_status == yomk::payload::ResStatus::Succeeded && infoDesc.m_msg == "ok",
              "4 参注册 get_info() 命中 m_msg 恒为 ok");
        auto descInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(infoDesc.m_data);
        YOMK_TEST_CHECK(descInfo != nullptr, "4 参注册 get_info() 结构化包恒挂");
        YOMK_TEST_CHECK(descInfo && descInfo->d.m_funcName == "fp_desc" && descInfo->d.m_msgName == "String",
              "4 参注册字段 m_funcName/m_msgName 一致");
        YOMK_TEST_CHECK(descInfo && descInfo->d.m_description == "fp description", "字段 m_description 承载函数说明");
        auto threeData = yomk::fn_pool().fn_name("fp_three").get_info();
        auto threeDescInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(threeData.m_data);
        YOMK_TEST_CHECK(threeDescInfo && threeDescInfo->d.m_description.empty(),
              "3 参注册（无说明）m_description 为空（结构化包恒挂）");

        // 覆盖语义：4 参后接 2 参同名热替换 → 类型后缀与说明均清除
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_desc").fn(echoFunc).install() == yomk::payload::ResStatus::Succeeded, "2 参覆盖 4 参注册返回 ResStatus::Succeeded");
        auto infoPlain = yomk::fn_pool().fn_name("fp_desc").get_info();
        auto plainInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(infoPlain.m_data);
        YOMK_TEST_CHECK(infoPlain.m_status == yomk::payload::ResStatus::Succeeded && infoPlain.m_msg == "ok" && plainInfo &&
                  plainInfo->d.m_funcName == "fp_desc" && plainInfo->d.m_msgName.empty() &&
                  plainInfo->d.m_description.empty(),
              "2 参热替换清除类型与说明（结构化字段为空）");

        // 清理
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_two").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_two");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_three").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_three");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_desc").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_desc");
    }

    // ============ Section 7: call() 异常穿透契约（显式契约）============
    {
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_throw").fn(throwFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 抛异常函数返回 ResStatus::Succeeded");

        // call() 链五层（request→invoke→callFunction→用户函数）无 try/catch：
        // 异常穿透直达调用方（区别于 EventLoop run() 的吞噬设计）
        bool caught = false;
        try
        {
            yomk::fn_pool().fn_name("fp_throw").msg(nullptr).call();
        }
        catch (const std::runtime_error& e)
        {
            caught = (std::string(e.what()) == "fp_boom");
        }
        YOMK_TEST_CHECK(caught, "用户函数异常穿透 call() 链直达调用方（what 一致）——契约行为");

        // 穿透后池存活：正常注册与调用不受影响
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_alive").fn(aliveFunc).install() == yomk::payload::ResStatus::Succeeded,
            "异常穿透后 install() 仍正常");
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name("fp_alive").msg(nullptr).call().m_msg.find("alive") != std::string::npos,
            "异常穿透后 call() 正常函数执行");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_throw").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_throw");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_alive").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_alive");
    }

    // ============ Section 8: 超长函数名边界 ============
    {
        std::string longName(65536, 'F');
        YOMK_TEST_CHECK(
            yomk::fn_pool().fn_name(longName).fn(aliveFunc).install() == yomk::payload::ResStatus::Succeeded,
            "install() 65536 字节函数名返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name(longName).msg(nullptr).call().m_status == yomk::payload::ResStatus::Succeeded, "call() 超长函数名返回 ResStatus::Succeeded");
        auto longInfo = yomk::fn_pool().fn_name(longName).get_info();
        YOMK_TEST_CHECK(longInfo.m_status == yomk::payload::ResStatus::Succeeded, "get_info() 超长函数名返回 ResStatus::Succeeded");
        auto longFuncInfo = yomk::msgs::unpack<yomk::msgs::FuncInfo>(longInfo.m_data);
        YOMK_TEST_CHECK(longFuncInfo && longFuncInfo->d.m_funcName == longName, "get_info() 完整回显超长函数名");
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name(longName).uninstall() == yomk::payload::ResStatus::Succeeded, "uninstall() 超长函数名返回 ResStatus::Succeeded");
    }

    // ============ 收尾清理（不污染后续闭环/其他测试的共享池）============
    {
        YOMK_TEST_CHECK(yomk::fn_pool().fn_name("fp_plain").uninstall() == yomk::payload::ResStatus::Succeeded, "清理 fp_plain");
    }

    yomk::server().shutdown();

    if (g_failed == 0)
    {
        std::cout << "TestYomkFunctionPoolLifecycle all check passed." << std::endl;
        return 0;
    }
    std::cout << "TestYomkFunctionPoolLifecycle FAILED (" << g_failed << " checks failed)." << std::endl;
    return 1;
}
