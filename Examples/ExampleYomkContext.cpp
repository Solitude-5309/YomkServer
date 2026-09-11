/**
 * @file ExampleYomkContext.cpp
 * @brief YomkContext 共享上下文快速上手示例（面向初学者）
 *
 * 上下文是全局共享的强类型键值状态机：把消息包按 key 存起来，
 * 任意位置按 key 读写——适合跨服务共享配置与运行时状态。
 * 在此之上还有两道可选防线：变更前校验门控（Checker，返回 eReject
 * 可拦截非法写入）与变更后通知（Monitor，写入成功后收到键值快照）。
 *
 * 本示例按 8 个步骤演示全部用法。每一步先打印横幅说明
 * "接下来做什么、预期看到什么"，再调用 API，随后用返回值、回调日志
 * 与内省结果自证行为——只读运行输出即可明白每个 API 调用发生了什么。
 *
 * 步骤总览：
 * 0. 框架就绪（上下文服务随 YOMK_INIT 自动启动）
 * 1. 创建与读取（CREATE / 重复创建 / GET 命中 / GET 未命中默认值兜底）
 * 2. 更新与强类型契约（SET / 类型不匹配被拒）
 * 3. Checker 门控（放行 / 拒绝 / 全局开关）
 * 4. Monitor 同步通知（写入成功后收到快照）
 * 5. Monitor 多播与异常防护（追加式多播；回调异常被框架吞掉）
 * 6. 异步通知与全局开关（池线程送达；OFF/ON_MONITOR）
 * 7. 内省与销毁（INFO 三件套 / DESTROY / not-found 分支）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_CONTEXT_CREATE        创建键值（key 已存在返回 eNo）
 *   YOMK_CONTEXT_GET           读取（未命中返回兜底默认值）
 *   YOMK_CONTEXT_SET           更新（值类型须与现值一致）
 *   YOMK_CONTEXT_SET_CHECKER   设置键级校验回调
 *   YOMK_CONTEXT_ON/OFF_CHECKER  全局校验开关
 *   YOMK_CONTEXT_SET_MONITOR   追加键级通知回调（可多个，async 可选）
 *   YOMK_CONTEXT_ON/OFF_MONITOR  全局通知开关
 *   YOMK_CONTEXT_INFO_KEYS     清单：全部键名
 *   YOMK_CONTEXT_INFO_KEY      单查：键状态行
 *   YOMK_CONTEXT_INFO_ALL      全量：每键一行状态
 *   YOMK_CONTEXT_DESTROY       销毁键（不存在返回 eNo）
 */

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "YomkAPI.h"

// 定点引入所需类型，避免全局 using
using yomk::ContextChecker;

// ---------------------------------------------------------------------------
// 辅助输出：横幅与返回值打印。
// 横幅走 std::cout 而非框架日志，保证叙事在任何日志开关状态下可见。
// ---------------------------------------------------------------------------

static void printStep(int n, const std::string& title, const std::string& explain)
{
    std::cout << "\n====== 步骤" << n << "：" << title << " ======" << std::endl;
    std::cout << ">> " << explain << std::endl;
}

// 打印 YomkResponse 三要素：status/msg/m_data，展示调用契约
static void printResp(const std::string& prefix, const YomkResponse& resp)
{
    // 三态：eOk=0 成功；eNo=1 名字不存在或被拒绝；eInvalid=-1 参数无效或未初始化
    std::cout << "[" << prefix << "] status=" << resp.m_status << ", msg=\"" << resp.m_msg << "\"";
    if (resp.m_data)
    {
        YomkUnPackPkg(resp.m_data, String, data);
        if (data)
        {
            std::cout << ", data=\"" << data->d << "\"";
        }
    }
    std::cout << std::endl;
}

// 解包并打印内省返回的 StringArray（INFO_KEYS / INFO_ALL 的返回形态）
static void dumpLines(const std::string& prefix, const YomkResponse& resp)
{
    printResp(prefix, resp);
    YomkUnPackPkg(resp.m_data, StringArray, arr);
    if (!arr)
    {
        std::cout << ">> (no data)" << std::endl;
        return;
    }
    for (const auto& line : arr->d)
    {
        std::cout << ">> | " << line << std::endl;
    }
}

// GET 返回 shared_ptr<String> 而非 YomkResponse，独立打印；
// isDefault 由调用方比较返回指针与兜底默认值指针得出
static void printGet(const std::string& prefix, YomkPtr(String) val, bool isDefault)
{
    std::cout << ">> [" << prefix << "] value=\"" << (val ? val->d : "(null)") << "\""
              << (isDefault ? " (default 兜底)" : "") << std::endl;
}

// ---------------------------------------------------------------------------
// Checker / Monitor 回调：用户代码，框架在 set 调用链的固定时机调用。
// Checker 在写锁内调用（回调内重入 set/get 会死锁）；Monitor 在写锁外执行。
// ---------------------------------------------------------------------------

// 放行型 Checker：非空 String 一律接受
ContextChecker::ECheckStatus checkerAcceptFunc(const yomk::Context& ctx)
{
    YomkUnPackPkg(ctx.m_value, String, str);
    if (!str)
    {
        YOMK_INFO_TAG("ctx.checker", "accept check: value is not String, reject");
        return ContextChecker::eReject;
    }
    YOMK_INFO_TAG("ctx.checker", "accept check: key=", ctx.m_key, ", value=", str->d, " -> eAccept");
    return ContextChecker::eAccept;
}

// 拒绝型 Checker：一律拒绝，用于保护关键配置不被修改
ContextChecker::ECheckStatus checkerRejectFunc(const yomk::Context& ctx)
{
    YOMK_INFO_TAG("ctx.checker", "reject check: key=", ctx.m_key, " -> eReject");
    return ContextChecker::eReject;
}

// 同步 Monitor：写入成功后（写锁外）收到本次 set 的键值快照
void monitorSyncFunc(const yomk::Context& ctx)
{
    YomkUnPackPkgVoid(ctx.m_value, String, str);
    YOMK_INFO_TAG("ctx.monitor", "sync got snapshot: key=", ctx.m_key, ", value=", str->d);
}

// 抛异常的 Monitor：验证框架防护——异常被吞，不影响 set 结果与后续 monitor
void monitorThrowFunc(const yomk::Context& /*ctx*/)
{
    throw std::runtime_error("monitor boom");
}

// 计数 Monitor：验证前置回调抛异常后后续 monitor 仍照常执行
static int monitorCallCount = 0;
void monitorCountFunc(const yomk::Context& /*ctx*/)
{
    ++monitorCallCount;
}

// 异步 Monitor：经单线程池送达（按 set 提交序保序），执行线程为池线程
void monitorAsyncFunc(const yomk::Context& ctx)
{
    YomkUnPackPkgVoid(ctx.m_value, String, str);
    YOMK_INFO_TAG(
        "ctx.monitor",
        "async got snapshot: key=",
        ctx.m_key,
        ", value=",
        str->d,
        " (pool thread: ",
        std::this_thread::get_id(),
        ")");
}

int main(int argc, char* argv[])
{
    // 初始化框架（上下文服务 /YomkContext 随之自动启动）
    YOMK_INIT();

    printStep(0, "框架就绪", "YOMK_INIT 已自动启动上下文服务；YOMK_VERSION 返回框架版本号。");
    printResp("YOMK_VERSION", YomkResponse(YomkResponse::eOk, YOMK_VERSION));

    /**
     * 步骤1：创建与读取
     *
     * CREATE 把消息包按 key 存入全局键值表；GET 按类型取出（未命中返回兜底默认值）。
     */
    printStep(
        1,
        "创建与读取",
        "CREATE(config, v1) 后 GET 命中；重复 CREATE 同名 key 被拒；GET 未注册的 ghost 返回兜底默认值。");
    printResp("CREATE(config,v1)", YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "v1")));
    printResp("CREATE(config,重复)", YOMK_CONTEXT_CREATE("config", YomkMkPtr(String, "other")));
    std::cout << ">> 同名 key 已存在 → eNo=1，创建是排他的；更新请用 SET" << std::endl;

    YomkPtr(String) fallback = YomkMkPtr(String, "fallback");
    printGet("GET(config)", YOMK_CONTEXT_GET(String, "config", fallback), false);
    YomkPtr(String) val = YOMK_CONTEXT_GET(String, "ghost", fallback);
    printGet("GET(ghost)", val, val == fallback);
    std::cout << ">> ghost 未注册 → 返回兜底默认值，调用方永不拿到空指针" << std::endl;

    /**
     * 步骤2：更新与强类型契约
     *
     * SET 整体替换值对象（先前 GET 的持有者仍是旧快照）；
     * 新值类型名必须与现值一致，否则被拒——强类型是统一契约。
     */
    printStep(
        2, "更新与强类型契约", "SET(config, v2) 成功后 GET 验证；换成 Int32 类型更新被拒：值类型须与创建时一致。");
    printResp("SET(config,v2)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v2")));
    printGet("GET(config)", YOMK_CONTEXT_GET(String, "config", fallback), false);
    printResp("SET(config,Int32)", YOMK_CONTEXT_SET("config", YomkMkPtr(Int32, 100)));
    std::cout << ">> 类型不匹配 → eNo=1（context type not match），强类型防误写" << std::endl;

    /**
     * 步骤3：Checker 门控
     *
     * SET_CHECKER 为键挂校验回调，ON_CHECKER 打开全局开关；
     * 生效条件 = 开关 ON 且该键已设 checker（未设视为放行）。
     */
    printStep(
        3,
        "Checker 门控",
        "accept checker 放行写入；换成 reject checker 后写入被拒且值不变；OFF_CHECKER 全局关闭后放行恢复。");
    printResp("SET_CHECKER(config,accept)", YOMK_CONTEXT_SET_CHECKER("config", checkerAcceptFunc));
    printResp("ON_CHECKER", YOMK_CONTEXT_ON_CHECKER());
    printResp("SET(config,v3,放行)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v3")));
    std::cout << ">> 注意 ctx.checker 日志：checker 在 set 调用链内被调用" << std::endl;

    printResp("SET_CHECKER(config,reject)", YOMK_CONTEXT_SET_CHECKER("config", checkerRejectFunc));
    printResp("SET(config,v4,被拒)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v4")));
    printGet("GET(config,值未变)", YOMK_CONTEXT_GET(String, "config", fallback), false);

    printResp("OFF_CHECKER", YOMK_CONTEXT_OFF_CHECKER());
    printResp("SET(config,v4,放行恢复)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v4")));
    std::cout << ">> 全局开关 OFF：checker 保留在键上但不生效；后续步骤专注演示 monitor" << std::endl;

    /**
     * 步骤4：Monitor 同步通知
     *
     * ON_MONITOR 打开全局开关后，每次 set 成功都会通知该键的全部 monitor；
     * 被 checker 拒绝的 set 不会走到通知（写入未发生）。
     */
    printStep(
        4, "Monitor 同步通知", "注册同步 monitor 后 SET：ctx.monitor 日志随即出现，回调收到的就是本次写入的键值快照。");
    printResp("ON_MONITOR", YOMK_CONTEXT_ON_MONITOR());
    printResp("SET_MONITOR(config,sync)", YOMK_CONTEXT_SET_MONITOR("config", monitorSyncFunc));
    printResp("SET(config,v5,触发通知)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v5")));
    std::cout << ">> 同步通知在 set 返回前执行（写锁外），日志先于下方返回行出现" << std::endl;

    /**
     * 步骤5：Monitor 多播与异常防护
     *
     * SET_MONITOR 是追加式：同一键可挂多个 monitor，按注册顺序全部执行；
     * 某个同步回调抛异常会被框架吞掉记日志——set 仍成功，后续 monitor 照常。
     */
    printStep(
        5, "多播与异常防护", "再追加 throw 与 count 两个 monitor 后 SET：异常日志出现但 set 仍 eOk，count 照常计数。");
    printResp("SET_MONITOR(config,throw)", YOMK_CONTEXT_SET_MONITOR("config", monitorThrowFunc));
    printResp("SET_MONITOR(config,count)", YOMK_CONTEXT_SET_MONITOR("config", monitorCountFunc));
    printResp("SET(config,v6,触发多播)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v6")));
    std::cout << ">> [自证] set 成功且 monitorCallCount=" << monitorCallCount << "（异常被吞，后续 monitor 未受影响）"
              << std::endl;

    /**
     * 步骤6：异步通知与全局开关
     *
     * async=true 的 monitor 经单线程池送达（按 set 提交序保序），
     * 执行线程是池线程；OFF_MONITOR 全局关闭期间通知暂停但 monitor 保留。
     */
    printStep(
        6,
        "异步通知与全局开关",
        "注册 async monitor 后 SET：通知稍后由池线程送达（对比线程 id）；OFF_MONITOR 期间 SET 无任何通知。");
    std::cout << ">> 主线程 id: " << std::this_thread::get_id() << std::endl;
    printResp("SET_MONITOR(config,async)", YOMK_CONTEXT_SET_MONITOR("config", monitorAsyncFunc, true));
    printResp("SET(config,v7,异步通知)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v7")));
    std::cout << ">> 等待异步通知送达便于观察（单线程池按提交序执行）" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    printResp("OFF_MONITOR", YOMK_CONTEXT_OFF_MONITOR());
    printResp("SET(config,v8,静默)", YOMK_CONTEXT_SET("config", YomkMkPtr(String, "v8")));
    std::cout << ">> 此条 set 无任何 ctx.monitor 日志：全局关闭，monitor 保留但不执行" << std::endl;
    printResp("ON_MONITOR", YOMK_CONTEXT_ON_MONITOR());

    /**
     * 步骤7：内省与销毁
     *
     * INFO_KEYS / INFO_KEY / INFO_ALL 查询键与注册状态；
     * DESTROY 删除键，之后的一切操作返回 eNo（not-found 惯例）。
     */
    printStep(7, "内省与销毁", "内省三件套看键清单与注册状态；DESTROY 后 GET 返回兜底默认值，重复 DESTROY 返回 eNo。");
    dumpLines("INFO_KEYS", YOMK_CONTEXT_INFO_KEYS());
    printResp("INFO_KEY(config)", YOMK_CONTEXT_INFO_KEY("config"));
    std::cout << ">> 状态行：类型 [String] / checker:on（已挂回调）/ monitors:4(async:1)（4 个回调 1 个异步）"
              << std::endl;
    printResp("INFO_KEY(ghost)", YOMK_CONTEXT_INFO_KEY("ghost"));
    dumpLines("INFO_ALL", YOMK_CONTEXT_INFO_ALL());

    printResp("DESTROY(config)", YOMK_CONTEXT_DESTROY("config"));
    val = YOMK_CONTEXT_GET(String, "config", fallback);
    printGet("GET(config,已销毁)", val, val == fallback);
    printResp("DESTROY(config,重复)", YOMK_CONTEXT_DESTROY("config"));
    dumpLines("INFO_ALL(收尾)", YOMK_CONTEXT_INFO_ALL());
    std::cout << ">> 键值表已空" << std::endl;

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
