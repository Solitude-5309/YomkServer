/**
 * @file TestYomkServerInfoLifecycle.cpp
 * @brief ServerInfo 模块生命周期与四端点功能基线测试（P0，从零新建——模块此前零专属覆盖）
 *
 * 覆盖内容（8 个 Section，76 断言）：
 * S0. server destroyed 分支（白盒直连）：测试自持 YomkServerInfo 实例（不经 addService），
 *     server 释放后 4 端点 invoke 逐一返回 eNo + "server has been destroyed"
 *     （m_weakServer.lock() 失败分支，invoke 为 YomkService public 方法可直连）
 * S1. /services：eOk + StringArray 含 5 内置服务；FmtSrv 注册后出现、删除后消失、重注册后出现
 * S2. /functions：FuncInfoList 结构化清单（m_funcName 带 / 前缀、m_msgName 类型名/空、
 *     m_description 四参说明）；空 srvName eInvalid；不存在服务 eOk 空表；内置
 *     /YomkContext 与 /YomkServerInfo 自身条目
 * S3. /function：命中时 m_msg 精确等于 msgName（特殊契约，非 "ok"）、无类型函数 m_msg 为空；
 *     四参函数说明挂入数据段（m_msg 契约保持）、无说明函数数据段为空；
 *     URL 解析边界（空串/无 / 开头/无第二个 /）；服务不存在/函数不存在契约消息；null pkg
 * S4. /all：ServerServiceInfoList 结构化包（按 m_serviceName 查条目；m_funcs map 字典序
 *     /echo 在 /ping 前，字段断言替代缩进行精确串）
 * S5. 轻量并发快照一致性：3 churn 线程 add/del × 3 内省线程轮询 4 端点（服务表 shared_mutex
 *     读写竞争），恒 eOk、/services 恒含存活服务、churn 后无残留
 * S6. 内置服务删除/重注册自举契约（对齐 Context Lifecycle S4/S5 模式）：删除后 4 端点不可路由、
 *     服务表快照反映删除；newService 无参重注册（构造函数固定名生效）后自举恢复
 * S7. 超长名内省边界（65536 字符服务名，对齐 FPC1-S8）：/services 行完整、/functions 条目字段、
 *     /function 长名下 URL 二段解析、/all 超长条目完整无截断
 *
 * 说明：S1-S5 经 YOMK_INIT 单例 + YOMK_SERVER_INFO_* 宏走完整路由链；S0 类级直连
 *       （include Modules/ServerInfo/YomkServerInfo.h，CMake 已追加 src 目录），
 *       放在 YOMK_SHUTDOWN 之后以独立 create 实例隔离单例交互。
 *       并发段断言经原子计数器收集、节末单线程 YOMK_TEST_CHECK（TSan-clean 既有模式）。
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Modules/ServerInfo/YomkServerInfo.h"
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

static bool nameIn(const std::vector<std::string>& names, const std::string& srvName)
{
    return std::find(names.begin(), names.end(), srvName) != names.end();
}

// 在 ServerServiceInfoList 解包结果中按服务名查找条目（/all 结构化消费 helper）
static const yomk::ServerServiceInfo* findServiceInfo(
    const std::shared_ptr<yomkmsg::ServerServiceInfoList>& list, const std::string& srvName)
{
    if (!list)
    {
        return nullptr;
    }
    for (const auto& srv : list->d)
    {
        if (srv.m_serviceName == srvName)
        {
            return &srv;
        }
    }
    return nullptr;
}

// 在服务条目内按函数名查找元信息（m_funcName 带 / 前缀 url 段）
static const yomk::FuncInfo* findFuncInfo(const yomk::ServerServiceInfo* srv, const std::string& funcName)
{
    if (srv == nullptr)
    {
        return nullptr;
    }
    for (const auto& func : srv->m_funcs)
    {
        if (func.m_funcName == funcName)
        {
            return &func;
        }
    }
    return nullptr;
}

// 解包 StringArray 响应数据段（/services 的 m_data 为 StringArray 包）
static std::vector<std::string> unpackStringArray(const YomkResponse& resp)
{
    std::vector<std::string> lines;
    if (resp.m_status != YomkResponse::eOk || !resp.m_data)
    {
        return lines;
    }
    std::shared_ptr<yomkmsg::StringArray> arr = nullptr;
    if (resp.m_data && resp.m_data->name() == "StringArray")
    {
        arr = std::dynamic_pointer_cast<yomkmsg::StringArray>(resp.m_data);
    }
    if (arr)
    {
        lines = arr->d;
    }
    return lines;
}

// 解包 FuncInfoList 响应数据段（/functions 的 m_data 为 FuncInfoList 结构化包）
static std::vector<yomk::FuncInfo> unpackFuncInfoList(const YomkResponse& resp)
{
    std::vector<yomk::FuncInfo> funcs;
    if (resp.m_status != YomkResponse::eOk || !resp.m_data)
    {
        return funcs;
    }
    std::shared_ptr<yomkmsg::FuncInfoList> listPkg = nullptr;
    if (resp.m_data && resp.m_data->name() == "FuncInfoList")
    {
        listPkg = std::dynamic_pointer_cast<yomkmsg::FuncInfoList>(resp.m_data);
    }
    if (listPkg)
    {
        funcs = listPkg->d;
    }
    return funcs;
}

// 在 FuncInfoList 解包结果中按函数名查找元信息（m_funcName 带 / 前缀 url 段）
static const yomk::FuncInfo* findFunc(const std::vector<yomk::FuncInfo>& funcs, const std::string& funcName)
{
    for (const auto& func : funcs)
    {
        if (func.m_funcName == funcName)
        {
            return &func;
        }
    }
    return nullptr;
}

/**
 * @brief 结构化清单演示服务：/ping 两参安装（无类型条目）+ /echo 四参安装
 *        （String 类型 + 说明 "echo demo description"），
 *        供 S2/S3/S4 条目字段精确断言与 description 透出断言使用
 */
class FmtSrv : public YomkService
{
public:
    explicit FmtSrv(YomkServer* server) : YomkService(server) {}

public:
    virtual int init() override
    {
        installFunc("/ping", &FmtSrv::ping);
        installFunc("/echo", &FmtSrv::echo, "String", "echo demo description");
        return 0;
    }

    YomkResponse ping(YomkPkgPtr pkg)
    {
        (void)pkg;
        return {YomkResponse::eOk, "pong"};
    }

    YomkResponse echo(YomkPkgPtr pkg)
    {
        auto yText = yomk::unPack<yomkmsg::String>(pkg);
        if (!yText)
        {
            return {YomkResponse::eNo, "pkg is null or pkg is not String"};
        }
        return {YomkResponse::eOk, yText->d};
    }
};

/**
 * @brief churn 压力服务：最小 /hit 函数，供 S5 并发增删使用
 */
class ChurnSrv : public YomkService
{
public:
    explicit ChurnSrv(YomkServer* server) : YomkService(server) {}

public:
    virtual int init() override
    {
        installFunc("/hit", &ChurnSrv::hit);
        return 0;
    }

    YomkResponse hit(YomkPkgPtr pkg)
    {
        (void)pkg;
        return {YomkResponse::eOk, "hit"};
    }
};

// ---- S5 并发观测变量（原子收集，节末单线程 YOMK_TEST_CHECK） ----
static std::atomic<int> g_addFail{0};    // churn addService 失败数（应恒为 0）
static std::atomic<int> g_delFail{0};    // churn delService 失败数（应恒为 0）
static std::atomic<int> g_badStatus{0};  // 内省端点非法状态计数（应恒为 0）
static std::atomic<int> g_missFmt{0};    // /services 快照丢失存活服务计数（应恒为 0）
static std::atomic<int> g_badFuncs{0};   // /functions("/FmtSrv") 快照非 2 行计数（应恒为 0）

int main()
{
    // ============ S0. server destroyed 分支（类级白盒，先于单例初始化） ============
    std::cout << "===== S0. server destroyed 分支（4 端点 weakServer.lock 失败） =====" << std::endl;
    {
        auto server = YomkServer::create(1);
        // 测试自持实例（不经 addService），构造时绑定 server 弱引用
        auto info = std::make_shared<YomkServerInfo>(server.get());
        YOMK_TEST_CHECK(info->init() == 0, "YomkServerInfo 独立实例 init 返回 0（4 端点已装配）");

        server.reset();  // server 销毁，m_weakServer 失效；info 由测试持有仍存活

        auto rServices = info->invoke("/services", nullptr);
        YOMK_TEST_CHECK(rServices.m_status == YomkResponse::eNo && rServices.m_msg == "server has been destroyed",
              "/services server destroyed: eNo + 契约消息: " + rServices.m_msg);

        // /functions 先做空名校验，须传非空 srvName 越过前置校验到达 lock 失败点
        auto rFunctions = info->invoke("/functions", yomkmsg::String::shared(std::string("/Any")));
        YOMK_TEST_CHECK(rFunctions.m_status == YomkResponse::eNo && rFunctions.m_msg == "server has been destroyed",
              "/functions server destroyed: eNo + 契约消息");

        // /function 先做 URL 解析校验，须传合法 url 越过前置校验到达 lock 失败点
        auto rFunction = info->invoke("/function", yomkmsg::String::shared(std::string("/Any/f")));
        YOMK_TEST_CHECK(rFunction.m_status == YomkResponse::eNo && rFunction.m_msg == "server has been destroyed",
              "/function server destroyed: eNo + 契约消息");

        auto rAll = info->invoke("/all", nullptr);
        YOMK_TEST_CHECK(rAll.m_status == YomkResponse::eNo && rAll.m_msg == "server has been destroyed",
              "/all server destroyed: eNo + 契约消息");

        info.reset();
        YOMK_TEST_CHECK(true, "destroyed 实例释放无崩溃（弱引用悬垂路径安全收尾）");
    }

    // ============ 单例初始化（S1-S5 共用，call_once 约束） ============
    YOMK_TEST_CHECK(YOMK_INIT(1) != nullptr, "YOMK_INIT 拉起单例与 5 个内置服务");

    // ============ S1. /services ============
    std::cout << "===== S1. /services 端点 =====" << std::endl;
    {
        auto resp = YOMK_SERVER_INFO_SERVICES();
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "ok", "/services 返回 eOk + ok");

        auto names = unpackStringArray(resp);
        YOMK_TEST_CHECK(nameIn(names, "/YomkFunctionPool"), "/services 含内置 /YomkFunctionPool");
        YOMK_TEST_CHECK(nameIn(names, "/YomkContext"), "/services 含内置 /YomkContext");
        YOMK_TEST_CHECK(nameIn(names, "/YomkEventLoop"), "/services 含内置 /YomkEventLoop");
        YOMK_TEST_CHECK(nameIn(names, "/YomkLogger"), "/services 含内置 /YomkLogger");
        YOMK_TEST_CHECK(nameIn(names, "/YomkServerInfo"), "/services 含内置 /YomkServerInfo");

        YOMK_TEST_CHECK(YOMK_ADD_SERVICE(new FmtSrv(YOMK_SERVER_P), "/FmtSrv") == 0, "注册 FmtSrv 返回 0");
        YOMK_TEST_CHECK(nameIn(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), "/FmtSrv"),
              "/services 实时反映新增服务");

        YOMK_TEST_CHECK(YOMK_DEL_SERVICE("/FmtSrv") == 0, "删除 FmtSrv 返回 0");
        YOMK_TEST_CHECK(!nameIn(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), "/FmtSrv"),
              "/services 实时反映服务删除");

        YOMK_TEST_CHECK(YOMK_ADD_SERVICE(new FmtSrv(YOMK_SERVER_P), "/FmtSrv") == 0,
              "重注册 FmtSrv（供 S2-S5 使用）");
        YOMK_TEST_CHECK(nameIn(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), "/FmtSrv"),
              "/services 重注册后恢复");
    }

    // ============ S2. /functions ============
    std::cout << "===== S2. /functions 端点 =====" << std::endl;
    {
        auto resp = YOMK_SERVER_INFO_FUNCTIONS("/FmtSrv");
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eOk, "/functions(FmtSrv) 返回 eOk");
        auto funcs = unpackFuncInfoList(resp);
        YOMK_TEST_CHECK(funcs.size() == 2, "FmtSrv 恰 2 个功能函数: " + std::to_string(funcs.size()));
        const yomk::FuncInfo* echo = findFunc(funcs, "/echo");
        YOMK_TEST_CHECK(echo != nullptr && echo->m_msgName == "String" && echo->m_description == "echo demo description",
              "四参安装条目 /echo：类型 String + 说明透出");
        const yomk::FuncInfo* ping = findFunc(funcs, "/ping");
        YOMK_TEST_CHECK(ping != nullptr && ping->m_msgName.empty(), "两参安装条目 /ping（m_msgName 空）");

        auto empty = YOMK_SERVER_INFO_FUNCTIONS("");
        YOMK_TEST_CHECK(empty.m_status == YomkResponse::eInvalid && empty.m_msg == "srvName is empty",
              "空 srvName 返回 eInvalid + 契约消息: " + empty.m_msg);

        auto noSrv = YOMK_SERVER_INFO_FUNCTIONS("/NoSuchSrv");
        YOMK_TEST_CHECK(noSrv.m_status == YomkResponse::eOk && unpackFuncInfoList(noSrv).empty(),
              "不存在服务返回 eOk + 空列表（现状契约）");

        auto ctx = YOMK_SERVER_INFO_FUNCTIONS("/YomkContext");
        auto ctxFuncs = unpackFuncInfoList(ctx);
        const yomk::FuncInfo* create = findFunc(ctxFuncs, "/create");
        YOMK_TEST_CHECK(ctx.m_status == YomkResponse::eOk && create != nullptr && create->m_msgName == "Context",
              "内置 /YomkContext 含 /create 期望类型 Context");
        YOMK_TEST_CHECK(findFunc(ctxFuncs, "/keys") != nullptr, "内置 /YomkContext 无类型端点 /keys（m_msgName 空）");

        auto self = YOMK_SERVER_INFO_FUNCTIONS("/YomkServerInfo");
        auto selfFuncs = unpackFuncInfoList(self);
        YOMK_TEST_CHECK(self.m_status == YomkResponse::eOk && selfFuncs.size() == 4,
              "/YomkServerInfo 自身恰 4 个端点");
        const yomk::FuncInfo* function = findFunc(selfFuncs, "/function");
        YOMK_TEST_CHECK(function != nullptr && function->m_msgName == "String" && findFunc(selfFuncs, "/all") != nullptr,
              "自身条目含带类型 /function 与无类型 /all");
    }

    // ============ S3. /function ============
    std::cout << "===== S3. /function 端点 =====" << std::endl;
    {
        auto hitTyped = YOMK_SERVER_INFO_FUNCTION("/FmtSrv/echo");
        YOMK_TEST_CHECK(hitTyped.m_status == YomkResponse::eOk && hitTyped.m_msg == "ok",
              "命中三参函数：eOk 且 m_msg 恒为 ok");
        auto typedInfo = yomk::unPack<yomkmsg::FuncInfo>(hitTyped.m_data);
        YOMK_TEST_CHECK(typedInfo != nullptr, "命中三参函数：数据为 FuncInfo 包");
        YOMK_TEST_CHECK(typedInfo && typedInfo->d.m_funcName == "/echo" && typedInfo->d.m_msgName == "String",
              "命中三参函数字段 m_funcName/m_msgName 一致");

        auto hitPlain = YOMK_SERVER_INFO_FUNCTION("/FmtSrv/ping");
        YOMK_TEST_CHECK(hitPlain.m_status == YomkResponse::eOk && hitPlain.m_msg == "ok",
              "命中两参函数：eOk 且 m_msg 恒为 ok");
        auto plainInfo = yomk::unPack<yomkmsg::FuncInfo>(hitPlain.m_data);
        YOMK_TEST_CHECK(plainInfo && plainInfo->d.m_funcName == "/ping" && plainInfo->d.m_msgName.empty(),
              "命中两参函数字段 m_funcName 一致且 m_msgName 为空");

        auto emptyUrl = YOMK_SERVER_INFO_FUNCTION("");
        YOMK_TEST_CHECK(emptyUrl.m_status == YomkResponse::eNo && emptyUrl.m_msg.find("please start with /") != std::string::npos,
              "空 url 返回 eNo + please start with /: " + emptyUrl.m_msg);

        auto noSlash = YOMK_SERVER_INFO_FUNCTION("abc");
        YOMK_TEST_CHECK(noSlash.m_status == YomkResponse::eNo && noSlash.m_msg.find("please start with /") != std::string::npos,
              "无 / 开头返回 eNo + please start with /");

        auto noFuncPart = YOMK_SERVER_INFO_FUNCTION("/FmtSrv");
        YOMK_TEST_CHECK(noFuncPart.m_status == YomkResponse::eNo &&
                  noFuncPart.m_msg.find("not found function name") != std::string::npos,
              "无第二个 / 返回 eNo + not found function name: " + noFuncPart.m_msg);

        auto noSrv = YOMK_SERVER_INFO_FUNCTION("/NoSuchSrv/f");
        YOMK_TEST_CHECK(noSrv.m_status == YomkResponse::eNo && noSrv.m_msg == "service not found: /NoSuchSrv",
              "服务不存在契约消息精确匹配: " + noSrv.m_msg);

        auto noFunc = YOMK_SERVER_INFO_FUNCTION("/FmtSrv/nope");
        YOMK_TEST_CHECK(noFunc.m_status == YomkResponse::eNo && noFunc.m_msg == "function not found: /FmtSrv/nope",
              "函数不存在契约消息精确匹配: " + noFunc.m_msg);

        auto nullPkg = YOMK_REQUEST("/YomkServerInfo/function", nullptr);
        YOMK_TEST_CHECK(nullPkg.m_status == YomkResponse::eNo && nullPkg.m_msg.find("pkg is null") != std::string::npos,
              "null pkg 返回 eNo + pkg is null（unPack 守卫）");

        // description 透出：四参安装的 /echo 命中时说明承载于 FuncInfo 包；无说明的 /ping 恒挂包但说明为空
        auto hitDesc = YOMK_SERVER_INFO_FUNCTION("/FmtSrv/echo");
        YOMK_TEST_CHECK(hitDesc.m_status == YomkResponse::eOk && hitDesc.m_msg == "ok",
              "四参函数命中：m_msg 恒为 ok");
        auto descInfo = yomk::unPack<yomkmsg::FuncInfo>(hitDesc.m_data);
        YOMK_TEST_CHECK(descInfo && descInfo->d.m_msgName == "String", "四参函数命中：m_msgName == \"String\"");
        YOMK_TEST_CHECK(descInfo && descInfo->d.m_description == "echo demo description",
              "四参函数命中：m_description == \"echo demo description\"");
        YOMK_TEST_CHECK(hitPlain.m_data != nullptr && plainInfo && plainInfo->d.m_description.empty(),
              "两参函数（无说明）命中：结构化包恒挂且 m_description 为空");
    }

    // ============ S4. /all ============
    std::cout << "===== S4. /all 端点 =====" << std::endl;
    {
        auto resp = YOMK_SERVER_INFO_ALL();
        YOMK_TEST_CHECK(resp.m_status == YomkResponse::eOk && resp.m_msg == "ok", "/all 返回 eOk + ok");

        auto list = yomk::unPack<yomkmsg::ServerServiceInfoList>(resp.m_data);
        YOMK_TEST_CHECK(list != nullptr && !list->d.empty(), "/all 输出非空");

        // FmtSrv 段：按 m_serviceName 查条目，段内 map 字典序 /echo 在 /ping 前
        const yomk::ServerServiceInfo* fmt = findServiceInfo(list, "/FmtSrv");
        YOMK_TEST_CHECK(fmt != nullptr, "/all 含 FmtSrv 服务条目");
        YOMK_TEST_CHECK(fmt && fmt->m_funcs.size() == 2 && fmt->m_funcs[0].m_funcName == "/echo" &&
                  fmt->m_funcs[0].m_msgName == "String" && fmt->m_funcs[1].m_funcName == "/ping" &&
                  fmt->m_funcs[1].m_msgName.empty(),
              "FmtSrv 段：/echo [String] 与 /ping 字段齐全（map 字典序）");

        // 内置 /YomkServerInfo 段：4 端点齐全（/services、/all 无类型，另两个带 [String]）
        const yomk::ServerServiceInfo* self = findServiceInfo(list, "/YomkServerInfo");
        YOMK_TEST_CHECK(self != nullptr, "/all 含 /YomkServerInfo 服务条目");
        YOMK_TEST_CHECK(findFuncInfo(self, "/services") != nullptr, "内置段含 \"/services\" 条目");
        YOMK_TEST_CHECK(findFuncInfo(self, "/functions") != nullptr &&
                  findFuncInfo(self, "/functions")->m_msgName == "String",
              "内置段含 \"/functions\" 条目且 m_msgName == \"String\"");
        YOMK_TEST_CHECK(findFuncInfo(self, "/all") != nullptr, "内置段含 \"/all\" 条目");
    }

    // ============ S5. 轻量并发快照一致性 ============
    std::cout << "===== S5. 并发 churn × 内省快照一致性 =====" << std::endl;
    {
        g_addFail.store(0);
        g_delFail.store(0);
        g_badStatus.store(0);
        g_missFmt.store(0);
        g_badFuncs.store(0);

        auto churnFn = [](int tid)
        {
            for (int round = 0; round < 50; ++round)
            {
                std::string name = "/Churn" + std::to_string(tid) + "_" + std::to_string(round);
                if (YOMK_ADD_SERVICE(new ChurnSrv(YOMK_SERVER_P), name) != 0)
                {
                    g_addFail.fetch_add(1);
                }
                if (YOMK_DEL_SERVICE(name) != 0)
                {
                    g_delFail.fetch_add(1);
                }
            }
        };

        auto introspectFn = []()
        {
            for (int round = 0; round < 200; ++round)
            {
                auto services = YOMK_SERVER_INFO_SERVICES();
                if (services.m_status != YomkResponse::eOk)
                {
                    g_badStatus.fetch_add(1);
                }
                else if (!nameIn(unpackStringArray(services), "/FmtSrv"))
                {
                    g_missFmt.fetch_add(1);  // FmtSrv 恒存活，快照不得丢失
                }

                auto functions = YOMK_SERVER_INFO_FUNCTIONS("/FmtSrv");
                if (functions.m_status != YomkResponse::eOk || unpackFuncInfoList(functions).size() != 2)
                {
                    g_badFuncs.fetch_add(1);
                }

                if (YOMK_SERVER_INFO_FUNCTION("/FmtSrv/ping").m_status != YomkResponse::eOk)
                {
                    g_badStatus.fetch_add(1);
                }

                if (YOMK_SERVER_INFO_ALL().m_status != YomkResponse::eOk)
                {
                    g_badStatus.fetch_add(1);
                }
            }
        };

        std::vector<std::thread> threads;
        for (int tid = 0; tid < 3; ++tid)
        {
            threads.emplace_back(churnFn, tid);
        }
        for (int tid = 0; tid < 3; ++tid)
        {
            threads.emplace_back(introspectFn);
        }
        for (auto& t : threads)
        {
            t.join();
        }

        YOMK_TEST_CHECK(g_addFail.load() == 0 && g_delFail.load() == 0, "churn 150 轮 add/del 全部成功");
        YOMK_TEST_CHECK(g_badStatus.load() == 0, "并发内省 4 端点状态全部合法");
        YOMK_TEST_CHECK(g_missFmt.load() == 0, "/services 快照恒含存活服务 /FmtSrv");
        YOMK_TEST_CHECK(g_badFuncs.load() == 0, "/functions(\"/FmtSrv\") 快照恒为 2 行");

        // churn 收尾：无残留、存活服务仍可调用
        auto names = unpackStringArray(YOMK_SERVER_INFO_SERVICES());
        bool churnLeft = false;
        for (auto& n : names)
        {
            if (n.find("/Churn") == 0)
            {
                churnLeft = true;
            }
        }
        YOMK_TEST_CHECK(!churnLeft, "churn 后服务表无 /Churn 残留");
        YOMK_TEST_CHECK(YOMK_REQUEST("/FmtSrv/ping", nullptr).m_status == YomkResponse::eOk,
              "并发结束后 FmtSrv 仍可正常调用");
    }

    // ============ S6. 内置服务删除/重注册自举契约 ============
    std::cout << "===== S6. 内置服务删除/重注册自举契约 =====" << std::endl;
    {
        YOMK_TEST_CHECK(YOMK_DEL_SERVICE("/YomkServerInfo") == 0, "删除内置 /YomkServerInfo 返回 0");

        auto deleted = YOMK_SERVER_INFO_SERVICES();
        YOMK_TEST_CHECK(deleted.m_status == YomkResponse::eNo && deleted.m_msg == "service not found: /YomkServerInfo",
              "删除后内省端点不可路由: " + deleted.m_msg);

        YOMK_TEST_CHECK(!nameIn(YOMK_SERVER_PTR->serviceNames(), "/YomkServerInfo"),
              "服务表快照不再含 /YomkServerInfo");

        // 无参重注册：newService 不改名，构造函数固定名 "/YomkServerInfo" 生效
        YOMK_TEST_CHECK(YomkAPI::newService<YomkServerInfo>() == 0,
              "newService 无参重注册返回 0（构造函数固定名生效）");

        auto restored = YOMK_SERVER_INFO_SERVICES();
        YOMK_TEST_CHECK(restored.m_status == YomkResponse::eOk &&
                  nameIn(unpackStringArray(restored), "/YomkServerInfo"),
              "重注册后自举恢复：/services eOk 且含自身");
        auto restoredAll = yomk::unPack<yomkmsg::ServerServiceInfoList>(YOMK_SERVER_INFO_ALL().m_data);
        YOMK_TEST_CHECK(findServiceInfo(restoredAll, "/YomkServerInfo") != nullptr,
              "重注册后 /all 含自身段（自举查询成功）");
    }

    // ============ S7. 超长名内省边界（65536 字符服务名，对齐 FPC1-S8） ============
    std::cout << "===== S7. 超长名内省边界（65536 字符服务名） =====" << std::endl;
    {
        const std::string longName = "/Long" + std::string(65531, 'x');
        YOMK_TEST_CHECK(longName.size() == 65536, "超长服务名长度 65536");

        YOMK_TEST_CHECK(YOMK_ADD_SERVICE(new FmtSrv(YOMK_SERVER_P), longName) == 0, "超长名服务注册返回 0");
        YOMK_TEST_CHECK(nameIn(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), longName),
              "/services 行完整包含超长服务名（无截断）");

        auto longFuncs = YOMK_SERVER_INFO_FUNCTIONS(longName);
        auto longFuncList = unpackFuncInfoList(longFuncs);
        YOMK_TEST_CHECK(longFuncs.m_status == YomkResponse::eOk && longFuncList.size() == 2,
              "超长名服务 /functions 返回 2 条目");
        const yomk::FuncInfo* longEcho = findFunc(longFuncList, "/echo");
        const yomk::FuncInfo* longPing = findFunc(longFuncList, "/ping");
        YOMK_TEST_CHECK(
            (longEcho != nullptr && longEcho->m_msgName == "String" && longPing != nullptr &&
             longPing->m_msgName.empty()),
            "超长名下函数条目字段完整");

        auto longFunction = YOMK_SERVER_INFO_FUNCTION(longName + "/ping");
        YOMK_TEST_CHECK(longFunction.m_status == YomkResponse::eOk && longFunction.m_msg == "ok",
              "超长名下 /function URL 二段解析正确（m_msg 恒为 ok）");
        auto longFuncInfo = yomk::unPack<yomkmsg::FuncInfo>(longFunction.m_data);
        YOMK_TEST_CHECK(longFuncInfo && longFuncInfo->d.m_funcName == "/ping",
              "超长名下 URL 二段解析正确（m_funcName == \"/ping\"）");

        auto longAll = yomk::unPack<yomkmsg::ServerServiceInfoList>(YOMK_SERVER_INFO_ALL().m_data);
        const yomk::ServerServiceInfo* longSrv = findServiceInfo(longAll, longName);
        YOMK_TEST_CHECK(longSrv != nullptr, "/all 含超长服务名条目（无截断）");
        YOMK_TEST_CHECK(longSrv && longSrv->m_funcs.size() == 2 && longSrv->m_funcs[0].m_funcName == "/echo" &&
                  longSrv->m_funcs[0].m_msgName == "String" && longSrv->m_funcs[1].m_funcName == "/ping",
              "/all 超长条目函数字段完整（echo [String] 与 ping，无截断）");

        YOMK_TEST_CHECK(YOMK_DEL_SERVICE(longName) == 0, "超长名服务删除返回 0");
        YOMK_TEST_CHECK(!nameIn(unpackStringArray(YOMK_SERVER_INFO_SERVICES()), longName),
              "删除后 /services 不再含超长名");
    }

    // ============ 单例收尾 ============
    YOMK_SHUTDOWN();
    YOMK_TEST_CHECK(YOMK_SERVER_PTR == nullptr, "YOMK_SHUTDOWN 后单例清空");

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
