/**
 * @file ExampleYomkService.cpp
 * @brief YomkService 服务定义与调用快速上手示例（面向初学者）
 *
 * "一切皆服务"：把业务逻辑封装为 YomkService 子类，注册到框架后
 * 任意位置用 URL "/服务名/函数名" 同步或异步调用。框架自动完成
 * 路由、弱绑定安全（服务删除后回调失效不崩溃）与调用拓扑自省。
 *
 * 本示例按 8 个步骤演示全部用法。多服务工程结构（组织方式本身也是教程）：
 * - msgs/DemoMsgs.h       消息包定义（服务间传输的数据结构）
 * - services/CalcService  计算服务（被 main 直接请求，内部跨服务调审计）
 * - services/AuditService 审计服务（被 CalcService 调用，含弱绑定回调）
 * - boot/DemoBoot         启动编排（before -> start -> after 三阶段）
 *
 * 每一步先打印横幅说明"接下来做什么、预期看到什么"，再调用 API，
 * 随后用返回值、服务内日志与自省结果自证行为——只读运行输出即可
 * 明白每个调用发生了什么。
 *
 * 步骤总览：
 * 0. 框架就绪（INIT / VERSION / 单例指针）
 * 1. 注册服务（NEW_SERVICE 一步到位；BOOT 多阶段编排）
 * 2. 同步请求（REQUEST 回包 / not-found 分支）
 * 3. 异步请求（ASYNC_REQUEST 回调线程自证）
 * 4. 跨服务调用与强类型回包（成员 request / AddResp）
 * 5. 服务自省（INFO_FUNCTIONS / FUNCTION：2 参 vs 3 参注册元数据）
 * 6. 服务管理（同名替换 / DEL_SERVICE 删除即停 / 失败传播）
 * 7. 优雅关闭（SHUTDOWN 后请求返回 eInvalid）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_INIT / YOMK_SHUTDOWN / YOMK_VERSION   框架生命周期
 *   YOMK_NEW_SERVICE(T, name)                  创建并注册服务（最常用）
 *   YOMK_ADD_SERVICE(ptr, name)                注册已有实例（同名即替换）
 *   YOMK_DEL_SERVICE(name)                     注销服务（删除即停）
 *   YOMK_BOOT(boot)                            启动编排（before -> start -> after）
 *   YOMK_REQUEST / YOMK_ASYNC_REQUEST          同步 / 异步请求
 *   YOMK_SERVER_P / YOMK_SERVER_PTR            全局单例
 *   YOMK_SERVER_INFO_SERVICES / FUNCTIONS / FUNCTION / ALL  调用拓扑自省
 *   YomkInstallFunc(2/3 参) / YomkBindWeakSelf / 成员 request  服务侧 API
 */

#include <chrono>
#include <iostream>
#include <thread>

#include "YomkAPI.h"
#include "boot/DemoBoot.h"
#include "msgs/DemoMsgs.h"
#include "services/AuditService.h"
#include "services/CalcService.h"

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
    // 三态：eOk=0 成功；eNo=1 目标不存在或被拒绝；eInvalid=-1 参数无效或未初始化
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

// 解包并打印自省返回的 StringArray（SERVER_INFO_* 的返回形态）
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

// 解包 add 的 AddResp 强类型回包，展示 m_data 不止能传 String
static void printAdd(const std::string& prefix, const YomkResponse& resp)
{
    printResp(prefix, resp);
    YomkUnPackPkg(resp.m_data, AddResp, respPkg);
    if (respPkg)
    {
        std::cout << ">> | AddResp.result=" << respPkg->resp.result << std::endl;
    }
}

int main(int argc, char* argv[])
{
    YOMK_INIT();

    printStep(0, "框架就绪", "YOMK_INIT 启动框架与 5 个内置服务；YOMK_SERVER_PTR 即全局单例。");
    printResp("YOMK_VERSION", YomkResponse(YomkResponse::eOk, YOMK_VERSION));
    std::cout << ">> [YOMK_SERVER_PTR] instance=" << (YOMK_SERVER_PTR ? "ready" : "null") << std::endl;

    printStep(
        1,
        "注册服务",
        "NEW_SERVICE(CalcService) 一步注册；BOOT(DemoBoot) 在 start 阶段注册 AuditService——注意日志顺序 "
        "before→start→after。");
    int newRet = YOMK_NEW_SERVICE(CalcService);
    std::cout << "[NEW_SERVICE(CalcService)] ret=" << newRet << std::endl;
    std::cout << ">> 返回 0 表示注册成功（服务已可用，URL 前缀 /CalcService）" << std::endl;
    int bootRet = YOMK_BOOT(new DemoBoot({"/AuditService"}));
    std::cout << "[BOOT(DemoBoot)] ret=" << bootRet << std::endl;
    dumpLines("INFO_SERVICES", YOMK_SERVER_INFO_SERVICES());
    std::cout << ">> 清单含 2 个新服务与 5 个内置服务" << std::endl;

    printStep(
        2,
        "同步请求",
        "REQUEST(/CalcService/echo) 返回回包 data；请求不存在的服务/函数分别得到 service/function not found。");
    printResp("REQUEST(echo)", YOMK_REQUEST("/CalcService/echo", YomkMkPtr(String, "hello")));
    printResp("REQUEST(服务不存在)", YOMK_REQUEST("/GhostService/echo", YomkMkPtr(String, "hello")));
    printResp("REQUEST(函数不存在)", YOMK_REQUEST("/CalcService/not_exist", YomkMkPtr(String, "hello")));
    std::cout << ">> not-found 均为 eNo=1，不崩溃——调用失败是普通返回值" << std::endl;

    printStep(3, "异步请求", "ASYNC_REQUEST 发送即返回；回调稍后在异步线程池执行（对比线程 id）。");
    std::cout << ">> 主线程 id: " << std::this_thread::get_id() << std::endl;
    YOMK_ASYNC_REQUEST(
        "/CalcService/echo",
        YomkMkPtr(String, "async hello"),
        [](YomkResponse resp)
        {
            std::cout << "[ASYNC_REQUEST 回调] status=" << resp.m_status << ", msg=\"" << resp.m_msg
                      << "\" (pool thread: " << std::this_thread::get_id() << ")" << std::endl;
        });
    std::cout << ">> 等待异步回调送达便于观察" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    printStep(
        4,
        "跨服务调用与强类型回包",
        "REQUEST(/CalcService/add) 一次请求触发调用链 main→add→audit（看 svc.calc/svc.audit 日志），回包 "
        "AddResp.result=7。");
    AddReq reqData{3, 4};
    printAdd("REQUEST(add)", YOMK_REQUEST("/CalcService/add", YomkMkPtr(AddReq, reqData)));

    printStep(
        5,
        "服务自省",
        "INFO_FUNCTIONS 看函数清单：/add [AddReq] 带 3 参注册元数据，/echo 无；INFO_FUNCTION 单查元数据。");
    dumpLines("INFO_FUNCTIONS(/CalcService)", YOMK_SERVER_INFO_FUNCTIONS("/CalcService"));
    printResp("INFO_FUNCTION(/CalcService/add)", YOMK_SERVER_INFO_FUNCTION("/CalcService/add"));
    printResp("INFO_FUNCTION(/CalcService/echo)", YOMK_SERVER_INFO_FUNCTION("/CalcService/echo"));
    std::cout << ">> [AddReq] 元数据由 YomkInstallFunc 第三参声明，供自省识别期望消息类型" << std::endl;

    printStep(
        6,
        "服务管理",
        "同名注册 AuditService 触发替换（实例编号 +1）；DEL_SERVICE 后 audit_hook 立即失效、add "
        "调用链失败传播；删除不存在的服务返回 -1。");
    std::cout << ">> [自证] 替换前 audit_hook 走旧实例：" << std::endl;
    printResp(
        "FUNCTIONPOOL_CALL(audit_hook)", YOMK_FUNCTIONPOOL_CALL("audit_hook", YomkMkPtr(String, "before replace")));
    AuditService* replaced = new AuditService(YOMK_SERVER_P);
    int addRet = YOMK_ADD_SERVICE(replaced, "/AuditService");
    std::cout << "[ADD_SERVICE(/AuditService,同名替换)] ret=" << addRet << std::endl;
    std::cout << ">> 框架日志可见 service already exists：旧实例被 markDeleted+deinit，新实例接管" << std::endl;
    printResp(
        "FUNCTIONPOOL_CALL(audit_hook,新实例)",
        YOMK_FUNCTIONPOOL_CALL("audit_hook", YomkMkPtr(String, "after replace")));
    int delRet = YOMK_DEL_SERVICE("/AuditService");
    std::cout << "[DEL_SERVICE(/AuditService)] ret=" << delRet << std::endl;
    printResp(
        "FUNCTIONPOOL_CALL(audit_hook,已删除)", YOMK_FUNCTIONPOOL_CALL("audit_hook", YomkMkPtr(String, "deleted")));
    std::cout << ">> 弱绑定回调返回 eNo（service has been deleted）：删除即停，不悬垂崩溃" << std::endl;
    printResp("REQUEST(add,审计缺失)", YOMK_REQUEST("/CalcService/add", YomkMkPtr(AddReq, reqData)));
    std::cout << ">> audit 缺失 → add 返回 eNo：跨服务调用失败沿调用链传播" << std::endl;
    int ghostRet = YOMK_DEL_SERVICE("/GhostService");
    std::cout << "[DEL_SERVICE(/GhostService)] ret=" << ghostRet << std::endl;

    printStep(7, "优雅关闭", "INFO_ALL 看终态拓扑；SHUTDOWN 后请求返回 eInvalid=-1（框架已关闭是普通返回值）。");
    dumpLines("INFO_ALL(终态)", YOMK_SERVER_INFO_ALL());
    YOMK_SHUTDOWN();
    std::cout << ">> YOMK_SHUTDOWN() 完成：排空在途请求并释放全部服务（幂等）" << std::endl;
    printResp("REQUEST(关闭后)", YOMK_REQUEST("/CalcService/echo", YomkMkPtr(String, "after shutdown")));

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
