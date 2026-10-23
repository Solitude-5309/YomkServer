/**
 * @file ExampleYomkServerInfo.cpp
 * @brief YomkServerInfo 服务自省快速上手示例（面向初学者）
 *
 * 自省即"运行期问框架"：/YomkServerInfo 是随 YOMK_INIT 自动启动的内置服务，
 * 提供服务清单、功能函数清单、单函数元信息与全量拓扑四类查询——调试与运维
 * 时用它确认"框架里现在有什么、每个功能函数期望什么消息类型"。
 *
 * 本示例按 7 个步骤演示全部用法。每一步先打印横幅说明
 * "接下来做什么、预期看到什么"，再调用 API，随后用返回值与自省结果
 * 自证行为——只读运行输出即可明白每个 API 调用发生了什么。
 *
 * 步骤总览：
 * 0. 框架就绪（5 个内置服务随 YOMK_INIT 启动，/YomkServerInfo 即其中之一）
 * 1. 注册演示服务（NEW_SERVICE 后清单多一项；REQUEST 证明注册即可调用）
 * 2. 函数清单（INFO_FUNCTIONS：2 参 vs 4 参注册的元数据差异 / 空清单分支）
 * 3. 单查元信息（INFO_FUNCTION 命中契约：m_msg=期望消息类型名、data 段=函数说明；4 个 eNo 分支）
 * 4. 全量拓扑（INFO_ALL：服务行 + 缩进函数行）
 * 5. 运行期实时视图（DEL_SERVICE 后清单与拓扑立即变化）
 * 6. 优雅关闭（SHUTDOWN 后自省返回 eInvalid，框架已关闭是普通返回值）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_INIT / YOMK_SHUTDOWN                   框架生命周期
 *   YOMK_NEW_SERVICE(T)                         创建并注册服务
 *   YOMK_REQUEST                                同步请求（自证注册的服务可用）
 *   YOMK_DEL_SERVICE(name)                      注销服务（删除即从拓扑消失）
 *   YOMK_SERVER_INFO_SERVICES()                 自省：全部服务名清单
 *   YOMK_SERVER_INFO_FUNCTIONS("/服务名")       自省：指定服务的功能函数清单
 *   YOMK_SERVER_INFO_FUNCTION("/服务名/函数名") 自省：单个功能函数元信息
 *   YOMK_SERVER_INFO_ALL()                      自省：框架全量服务拓扑
 *   YomkInstallFunc(2/3/4 参)                   服务侧注册（3 参声明期望消息类型，4 参附函数说明）
 *
 * 注：日志/上下文/事件循环/函数池各内置模块另有模块级自省 API
 * （YOMK_LOGGER_INFO_* 等），见 Docs/YomkServerInfo.md"说明"一节。
 */

#include <cstdint>
#include <iostream>
#include <string>

#include "YomkAPI.h"

// ---------------------------------------------------------------------------
// 自省素材：一个最小演示服务。2 参注册的 /hi 无元数据，4 参注册的 /sum
// 声明期望消息类型 SumReq 并附函数说明——差异将由步骤2/3 的自省结果直接呈现。
// ---------------------------------------------------------------------------

// 加法请求：此处仅作 /sum 的期望消息类型元数据，供自省识别
struct SumReq
{
    std::int32_t a;
    std::int32_t b;
};

YomkMsg(SumReq, SumReq, req)

class InfoDemoService : public YomkService
{
public:
    explicit InfoDemoService(YomkServer* server) : YomkService(server)
    {
        name("/InfoDemoService");  // 服务名：URL 前缀，必须 / 开头且全局唯一
    }

    int init() override
    {
        // 2 参注册：无元数据；4 参注册：声明 /sum 期望 SumReq 并附函数说明，供自省识别
        YomkInstallFunc("/hi", InfoDemoService::hi);
        YomkInstallFunc("/sum", InfoDemoService::sum, SumReq, "计算两整数之和");
        YOMK_INFO_TAG("svc.info", "init: install /hi (no meta), /sum [SumReq] + description");
        return 0;
    }

    // 问好：收到字符串回显进 m_msg
    YomkResponse hi(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, String, str);
        if (!str)
        {
            return {YomkResponse::eNo, "pkg is not String"};
        }
        YOMK_INFO_TAG("svc.info", name(), " hi got: ", str->d);
        return {YomkResponse::eOk, "hi " + str->d};
    }

    // 加法：解包 SumReq，把结果放进 m_msg
    YomkResponse sum(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, SumReq, data);
        if (!data)
        {
            return {YomkResponse::eNo, "pkg is not SumReq"};
        }
        YOMK_INFO_TAG("svc.info", name(), " sum got: ", data->req.a, " + ", data->req.b);
        return {YomkResponse::eOk, "sum = " + std::to_string(data->req.a + data->req.b)};
    }
};

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

int main(int argc, char* argv[])
{
    YOMK_INIT();

    /**
     * 步骤0：框架就绪
     *
     * YOMK_INIT 自动启动 5 个内置服务，自省服务 /YomkServerInfo 开箱即用，
     * 无需注册。INFO_SERVICES 查到的就是这张运行期服务表。
     */
    printStep(0, "框架就绪", "YOMK_INIT 启动框架与 5 个内置服务；/YomkServerInfo 即其中之一，自省开箱即用。");
    dumpLines("INFO_SERVICES", YOMK_SERVER_INFO_SERVICES());
    std::cout << ">> 清单即内置服务：自省查询的就是框架运行期服务表" << std::endl;

    /**
     * 步骤1：注册演示服务
     *
     * NEW_SERVICE 一步注册；注册后清单立即多一项，
     * REQUEST /hi 与 /sum 证明注册的服务真的可用。
     */
    printStep(1, "注册演示服务", "NEW_SERVICE(InfoDemoService) 后清单多一项；REQUEST /hi 与 /sum 证明注册即可调用。");
    int newRet = YOMK_NEW_SERVICE(InfoDemoService);
    std::cout << "[NEW_SERVICE(InfoDemoService)] ret=" << newRet << std::endl;
    dumpLines("INFO_SERVICES", YOMK_SERVER_INFO_SERVICES());
    printResp("REQUEST(hi)", YOMK_REQUEST("/InfoDemoService/hi", YomkMkPtr(String, "yomk")));
    SumReq sumReq{3, 4};
    printResp("REQUEST(sum)", YOMK_REQUEST("/InfoDemoService/sum", YomkMkPtr(SumReq, sumReq)));

    /**
     * 步骤2：函数清单
     *
     * INFO_FUNCTIONS 列出指定服务的全部功能函数：4 参注册的 /sum 带
     * [SumReq] 元数据，2 参注册的 /hi 没有；查不存在的服务返回空清单。
     */
    printStep(
        2, "函数清单", "INFO_FUNCTIONS 看函数清单：/sum [SumReq] 带 4 参注册元数据，/hi 无；查不存在的服务返回空清单。");
    dumpLines("INFO_FUNCTIONS(/InfoDemoService)", YOMK_SERVER_INFO_FUNCTIONS("/InfoDemoService"));
    std::cout << ">> [SumReq] 即期望消息类型元数据，由 YomkInstallFunc 第三参声明" << std::endl;
    dumpLines("INFO_FUNCTIONS(/GhostService)", YOMK_SERVER_INFO_FUNCTIONS("/GhostService"));
    std::cout << ">> 服务不存在 → 返回空清单（eOk），查询失败不崩溃" << std::endl;

    /**
     * 步骤3：单查元信息
     *
     * INFO_FUNCTION 按 URL 单查：命中返回结构化 FuncInfo 包（m_msg 恒 "ok"），
     * m_msgName=期望消息类型名、m_description=注册时声明的函数说明（均为可空字段）；
     * 4 种坏 URL 均返回 eNo，是普通返回值不崩溃。
     */
    printStep(
        3,
        "单查元信息",
        "INFO_FUNCTION 命中返回结构化 FuncInfo 包（m_msg 恒 \"ok\"）：m_msgName 承载期望消息类型名"
        "（/sum 为 SumReq，/hi 为空串），m_description 承载函数说明；4 种坏 URL 均为 eNo=1.");
    printResp("INFO_FUNCTION(/sum)", YOMK_SERVER_INFO_FUNCTION("/InfoDemoService/sum"));
    printResp("INFO_FUNCTION(/hi)", YOMK_SERVER_INFO_FUNCTION("/InfoDemoService/hi"));
    std::cout << ">> 命中契约：结构化 FuncInfo 包，m_msgName=期望的消息类型名，m_description=YomkInstallFunc 第四参声明的函数说明" << std::endl;
    printResp("INFO_FUNCTION(url不以/开头)", YOMK_SERVER_INFO_FUNCTION("InfoDemoService/sum"));
    printResp("INFO_FUNCTION(缺函数名)", YOMK_SERVER_INFO_FUNCTION("/InfoDemoService"));
    printResp("INFO_FUNCTION(服务不存在)", YOMK_SERVER_INFO_FUNCTION("/GhostService/hi"));
    printResp("INFO_FUNCTION(函数不存在)", YOMK_SERVER_INFO_FUNCTION("/InfoDemoService/ghost"));
    std::cout << ">> URL 解析失败 / 目标不存在均为 eNo=1，调用失败是普通返回值" << std::endl;

    /**
     * 步骤4：全量拓扑
     *
     * INFO_ALL 一次拉取全部服务及其功能函数：服务名一行、函数行缩进两格。
     */
    printStep(4, "全量拓扑", "INFO_ALL 输出全部服务及各自的功能函数：服务名一行，函数行缩进两格。");
    dumpLines("INFO_ALL", YOMK_SERVER_INFO_ALL());
    std::cout << ">> 6 个服务 = 5 个内置 + 1 个演示；排障时一眼看清调用拓扑" << std::endl;

    /**
     * 步骤5：运行期实时视图
     *
     * 自省读的是运行期服务表：DEL_SERVICE 后清单与拓扑立即少一项，
     * 无需重启或刷新——注册/删除即时反映到下一次查询。
     */
    printStep(5, "运行期实时视图", "DEL_SERVICE 删除演示服务后，INFO_SERVICES 与 INFO_ALL 立即少一项——自省是实时视图。");
    int delRet = YOMK_DEL_SERVICE("/InfoDemoService");
    std::cout << "[DEL_SERVICE(/InfoDemoService)] ret=" << delRet << std::endl;
    dumpLines("INFO_SERVICES", YOMK_SERVER_INFO_SERVICES());
    dumpLines("INFO_ALL(终态)", YOMK_SERVER_INFO_ALL());
    std::cout << ">> 删除即从拓扑消失，剩下的 5 项即内置服务" << std::endl;

    /**
     * 步骤6：优雅关闭
     *
     * SHUTDOWN 后框架已关闭，任何自省调用返回 eInvalid=-1（普通返回值）。
     */
    printStep(6, "优雅关闭", "SHUTDOWN 后自省返回 eInvalid=-1：框架已关闭是普通返回值，不崩溃。");
    YOMK_SHUTDOWN();
    std::cout << ">> YOMK_SHUTDOWN() 完成：排空在途请求并释放全部服务（幂等）" << std::endl;
    printResp("INFO_SERVICES(关闭后)", YOMK_SERVER_INFO_SERVICES());

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
