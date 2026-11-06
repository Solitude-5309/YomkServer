/**
 * @file TestYomkUmbrella.cpp
 * @brief 伞头自足性保障测试：仅包含 <YomkServer/YomkApi.h> 即可使用全部公开表面
 *
 * 决议背景（闭环 6 后用户要求）：外部用户只引入 YomkApi.h 一个框架头，就必须能触达
 * 六个域入口（server / ctx / log / log_mgr / event_loop / fn_pool）、YomkBoot /
 * YomkService / YomkServer 类型、YOMK_DEFINE_MSG / YOMK_DIAG_* 宏与 yomk::url /
 * yomk::payload 命名空间。本测试是这条保证的编译期锚点——任何人收窄伞头、新增公开头
 * 而漏进伞头、或把公开类型挪进未导出头，本文件编译即失败；运行段则对各面各打一枚
 * 代表性烟熏断言（写+读+守卫）。深度覆盖不归本测试（各域专项全面测试负责）。
 *
 * 纪律：除 C++ 标准头外，本文件禁止出现任何其它 #include；新增包含即违背测试初衷。
 */
#include <iostream>
#include <memory>
#include <string>

#include "YomkApi.h"

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

// —— 自定义消息类型：YOMK_DEFINE_MSG 宏经伞头可用（结构体先行定义）——
struct UmbrellaReq
{
    std::string text;
};
YOMK_DEFINE_MSG(::UmbrellaReq, UmbrellaReq, req);

// —— 自定义服务：YomkService 派生 + srv<T>().create() 注册链路 ——
class UmbrellaSrv : public YomkService
{
public:
    explicit UmbrellaSrv(YomkServer* server = nullptr) : YomkService(server) {}
    int init() override { return 0; }
};

// —— 启动编排：YomkBoot 三段全打点 ——
class UmbrellaBoot : public YomkBoot
{
public:
    int before() override { return 0; }
    int start() override { return 0; }
    int after() override { return 0; }
};

static yomk::payload::Response echoFunc(YomkMsgPtr pkg)
{
    return {yomk::payload::ResStatus::Succeeded, "echo", pkg};
}

int main()
{
    using yomk::payload::ResStatus;

    // ===== S1. 生命周期 / 版本 / 原始请求通道 =====
    YOMK_TEST_CHECK(!yomk::server().version().empty(), "version() 非空（伞头宏可见）");
    yomk::server().async_thread(1).init();
    YOMK_TEST_CHECK(yomk::server().instance() != nullptr, "async_thread(1).init() 后 instance() 非空");
    YOMK_TEST_CHECK(yomk::server().url(yomk::url::srvServices).request().m_status == ResStatus::Succeeded,
        "url(srvServices).request() 原始通道可用");
    YOMK_TEST_CHECK(yomk::server().services().m_status == ResStatus::Succeeded, "services() 内省可用");
    YOMK_TEST_CHECK(yomk::server().all().m_status == ResStatus::Succeeded, "all() 内省可用");

    // ===== S2. 服务管理（create / add / remove） =====
    YOMK_TEST_CHECK(yomk::server().srv<UmbrellaSrv>("/UmbrellaSrv").create() == 0, "srv<T>(name).create() 返回 0");
    YOMK_TEST_CHECK(yomk::server().srv_name("/UmbrellaSrv").remove() == 0, "srv_name(name).remove() 返回 0");
    YOMK_TEST_CHECK(
        yomk::server().srv_name("/UmbrellaRaw").srv_ptr(new UmbrellaSrv(yomk::server().instance().get())).add() == 0,
        "srv_name+srv_ptr.add() 返回 0");
    YOMK_TEST_CHECK(yomk::server().srv_name("/UmbrellaRaw").remove() == 0, "已有实例注销返回 0");

    // ===== S3. 启动编排 boot_ptr().run() =====
    YOMK_TEST_CHECK(yomk::server().boot_ptr(new UmbrellaBoot).run() == 0, "boot_ptr(cfg).run() 三段全过返回 0");

    // ===== S4. ctx 域：写 / 类型化读 / 清单 / 组开关 / 销毁 =====
    YOMK_TEST_CHECK(yomk::ctx().key("umb_key").value(yomk::msgs::String::shared(std::string("v"))).create() ==
                        ResStatus::Succeeded,
        "ctx().create() 成功");
    YOMK_TEST_CHECK(
        yomk::ctx().key("umb_key").value(yomk::msgs::String::shared(std::string("v2"))).set() == ResStatus::Succeeded,
        "ctx().set() 成功");
    auto snap = yomk::ctx().key("umb_key").msg<yomk::msgs::String>().with_default(yomk::msgs::String::shared(std::string("d"))).get();
    YOMK_TEST_CHECK(snap && snap->d == "v2", "ctx().msg<String>().get() 读到最新值");
    YOMK_TEST_CHECK(yomk::ctx().keys().m_status == ResStatus::Succeeded, "ctx().keys() 成功");
    YOMK_TEST_CHECK(yomk::ctx().group("main").checker().enable() == ResStatus::Succeeded, "组级 checker().enable() 成功");
    YOMK_TEST_CHECK(yomk::ctx().key("umb_key").destroy() == ResStatus::Succeeded, "ctx().destroy() 成功");

    // ===== S5. log / log_mgr 域 =====
    YOMK_TEST_CHECK(yomk::log().tag("umbrella").info("umbrella smoke") == ResStatus::Succeeded, "log().info() 成功");
    YOMK_TEST_CHECK(yomk::log_mgr().location().show_line(true).apply() == ResStatus::Succeeded,
        "log_mgr().location().apply() 成功");
    YOMK_DIAG_ERROR("umbrella diag smoke");  // 诊断宏经伞头可用（仅编译 + 不崩即证）

    // ===== S6. fn_pool 域：install / get_info / list / call / all / uninstall =====
    YOMK_TEST_CHECK(yomk::fn_pool()
                        .fn_name("umb_fn")
                        .fn(echoFunc)
                        .msg_name("UmbrellaReq")
                        .description("umbrella smoke fn")
                        .install() == ResStatus::Succeeded,
        "fn_pool().install() 成功");
    YOMK_TEST_CHECK(yomk::fn_pool().fn_name("umb_fn").get_info().m_status == ResStatus::Succeeded, "get_info() 成功");
    YOMK_TEST_CHECK(yomk::fn_pool().list().m_status == ResStatus::Succeeded, "list() 成功");
    YOMK_TEST_CHECK(yomk::fn_pool().fn_name("umb_fn").msg(yomk::msgs::UmbrellaReq::shared(UmbrellaReq{"ping"})).call().m_msg == "echo",
        "自定义消息 call() 透传成功");
    YOMK_TEST_CHECK(yomk::fn_pool().all().m_status == ResStatus::Succeeded, "all() 成功");
    YOMK_TEST_CHECK(yomk::fn_pool().fn_name("umb_fn").uninstall() == ResStatus::Succeeded, "uninstall() 成功");

    // ===== S7. event_loop 域：start / post_wait / list / destroy =====
    YOMK_TEST_CHECK(yomk::event_loop().loop("umb_loop").handle_fn(nullptr).start() == ResStatus::Succeeded,
        "event_loop().start() 成功");
    YOMK_TEST_CHECK(
        yomk::event_loop().msg(yomk::msgs::String::shared(std::string("d"))).loop("umb_loop").post_wait() ==
            ResStatus::Succeeded,
        "post_wait() 成功");
    YOMK_TEST_CHECK(yomk::event_loop().list().m_status == ResStatus::Succeeded, "event_loop().list() 成功");
    YOMK_TEST_CHECK(yomk::event_loop().loop("umb_loop").destroy() == ResStatus::Succeeded, "event_loop().destroy() 成功");

    // ===== S8. shutdown 守卫：关闭后读面回归 Invalid =====
    yomk::server().shutdown();
    YOMK_TEST_CHECK(yomk::server().instance() == nullptr, "shutdown 后 instance() 空");
    YOMK_TEST_CHECK(yomk::server().all().m_status == ResStatus::Invalid, "shutdown 后 all() 返回 Invalid");

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << std::endl;
    return g_failed;
}
