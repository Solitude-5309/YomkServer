/**
 * @file ExampleYomkEventLoop.cpp
 * @brief YomkEventLoop 线程隔离事件循环快速上手示例（面向初学者）
 *
 * 事件循环为特定业务提供一条单线程 FIFO 执行队列：投递进同一循环的
 * 事件按入队顺序在该循环的专用线程中逐个执行，循环之间彼此并行，
 * 适合把"必须串行"的业务（如状态机、账本更新）从锁竞争里解耦出来。
 *
 * 本示例按 8 个步骤演示全部用法。每一步先打印横幅说明
 * "接下来做什么、预期看到什么"，再调用 API，随后用返回值与内省
 * 结果自证行为——只读运行输出即可明白每个 API 调用发生了什么。
 *
 * 步骤总览：
 * 0. 框架就绪（事件循环服务随 YOMK_INIT 自动启动）
 * 1. 启动循环（默认处理函数 + 3 参 MsgName 元数据）
 * 2. 异步投递 POST + tag 队列观察 + FIFO 递归投递
 * 3. 同步投递 POST_WAIT（Event 三要素回传）
 * 4. 临时处理函数（优先于默认函数）
 * 5. 停止与续跑（STOP 保留队列，START 幂等重启）
 * 6. 销毁与 not-found（DESTROY 清空队列不可续跑）
 * 7. 全量自省收尾（INFO_ALL）
 *
 * 涉及的 API 宏（YomkAPI.h）：
 *   YOMK_EVENTLOOP_START       启动循环（1/2/3 参：名称 [+ 默认函数 [+ MsgName]]）
 *   YOMK_EVENTLOOP_STOP        停止线程（保留队列事件）
 *   YOMK_EVENTLOOP_POST        异步投递（可带临时函数与 tag）
 *   YOMK_EVENTLOOP_POST_WAIT   同步投递（阻塞等待执行完毕，回传 Event）
 *   YOMK_EVENTLOOP_DESTROY     销毁循环（清空队列，移出循环表）
 *   YOMK_EVENTLOOP_INFO_LOOPS  清单：全部循环名
 *   YOMK_EVENTLOOP_INFO_LOOP   单查：循环状态 + 队列 tag 预览
 *   YOMK_EVENTLOOP_INFO_ALL    全量：每循环一行状态
 */

#include <chrono>
#include <iostream>
#include <thread>

#include "YomkAPI.h"

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

// 解包并打印内省返回的 StringArray（INFO_LOOPS / INFO_ALL 的返回形态）
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

// INFO_LOOP 的状态行放在 msg 里，追加一行清单式展示便于对齐阅读
static void dumpInfo(const std::string& prefix, const YomkResponse& resp)
{
    printResp(prefix, resp);
    std::cout << ">> | " << resp.m_msg << std::endl;
}

// 解包 POST_WAIT 回传的 Event 包，打印 eventId/loopName/response 三要素
static void printEvent(const YomkResponse& resp)
{
    YomkUnPackPkg(resp.m_data, Event, event);
    if (!event)
    {
        std::cout << ">> (no event data)" << std::endl;
        return;
    }
    std::cout << ">> | eventId=" << event->d.m_eventId << ", loopName=" << event->d.m_eventLoopName << ", response=\""
              << event->d.m_response.m_msg << "\"" << std::endl;
}

// ---------------------------------------------------------------------------
// 事件处理函数：签名统一为 YomkResponse(YomkPkgPtr)。
// 可作为默认处理函数（启动时指定）或临时处理函数（投递时指定）。
// ---------------------------------------------------------------------------

YomkResponse workHandle(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, str);
    // 打印执行线程：同一循环内所有事件都在这同一条专用线程上执行
    YOMK_INFO_TAG("loop.work", "exec event: ", str->d, " (thread: ", std::this_thread::get_id(), ")");

    // 模拟耗时任务：让队列积压与 FIFO 消费顺序在输出中稳定可见
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 演示处理函数内递归投递：仅 task1 会追加 3 条 followup 事件到队尾，
    // 它们排在已入队的 task2..task5 之后消费——同循环 FIFO 的直观证据
    if (str->d == "task1")
    {
        for (int i = 1; i <= 3; ++i)
        {
            YOMK_EVENTLOOP_POST(
                "workLoop", YomkMkPtr(String, "followup" + std::to_string(i)), nullptr, "followup" + std::to_string(i));
        }
        YOMK_INFO_TAG("loop.work", "task1 posted 3 followup events to queue tail");
    }

    return {YomkResponse::eOk, "eventHandle success. "};
}

int main(int argc, char* argv[])
{
    // 初始化框架（事件循环服务 /YomkEventLoop 随之自动启动）
    YOMK_INIT();

    printStep(0, "框架就绪", "YOMK_INIT 已自动启动事件循环服务；YOMK_VERSION 返回框架版本号。");
    printResp("YOMK_VERSION", YomkResponse(YomkResponse::eOk, YOMK_VERSION));

    /**
     * 步骤1：启动事件循环（3 参：名称 + 默认处理函数 + MsgName 元数据）
     *
     * START 为该名称新建一条专用线程；默认处理函数处理所有未指定函数的事件；
     * MsgName 仅作内省元数据。对已存在的名称再次 START 是幂等重启。
     */
    printStep(
        1,
        "启动事件循环",
        "START(workLoop, workHandle, String) 新建专用线程并注册默认处理函数；INFO_LOOP 查看循环状态。");
    printResp("START(workLoop)", YOMK_EVENTLOOP_START("workLoop", workHandle, String));
    dumpInfo("INFO_LOOP(workLoop)", YOMK_EVENTLOOP_INFO_LOOP("workLoop"));

    /**
     * 步骤2：异步投递 + tag 队列观察 + FIFO
     *
     * POST 把事件放进循环队列后立即返回，不阻塞当前线程；
     * tag 仅内省可见，用于观察队列；同循环内事件按入队顺序执行。
     */
    printStep(
        2, "异步投递与队列观察", "连续 POST 5 条任务（每条处理 200ms）：POST 立即返回不阻塞；tag 用于观察队列积压。");
    for (int i = 1; i <= 5; ++i)
    {
        printResp(
            "POST(task" + std::to_string(i) + ")",
            YOMK_EVENTLOOP_POST(
                "workLoop", YomkMkPtr(String, "task" + std::to_string(i)), nullptr, "task" + std::to_string(i)));
    }
    std::cout << ">> 立即内省：正在执行的事件不计入 pending，队首列出的是待处理任务的 tag" << std::endl;
    dumpInfo("INFO_LOOP(workLoop,5)", YOMK_EVENTLOOP_INFO_LOOP("workLoop", 5));
    std::cout << ">> 等待队列排空，观察 loop.work 日志的消费顺序：task2..task5 之后才是 followup1..3（FIFO）"
              << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    dumpInfo("INFO_LOOP(workLoop,5)", YOMK_EVENTLOOP_INFO_LOOP("workLoop", 5));

    /**
     * 步骤3：同步投递（POST_WAIT）
     *
     * 阻塞直到该事件在循环线程中执行完毕，返回的 m_data 是 Event 包：
     * m_eventId（单调递增）、m_eventLoopName、m_response（处理函数返回值）。
     */
    printStep(
        3,
        "同步投递（POST_WAIT）",
        "syncTask 排到队尾等待执行完毕才返回；Event 三要素证明同步等待排在已投递任务之后。");
    YomkResponse resp = YOMK_EVENTLOOP_POST_WAIT("workLoop", YomkMkPtr(String, "syncTask"));
    printResp("POST_WAIT(syncTask)", resp);
    printEvent(resp);

    /**
     * 步骤4：临时处理函数
     *
     * 投递时指定 eventHandle 则优先于默认处理函数，同一循环可混用两种来源。
     */
    printStep(4, "临时处理函数", "tempTask 投递时指定临时函数：返回临时函数的结果而非默认函数的结果。");
    // lambda 先赋给具名变量再传宏：返回值构造含逗号时，
    // 直接内联进宏会让预处理器把逗号误当实参分隔符
    YomkServiceFunc tempHandle = [](YomkPkgPtr) -> YomkResponse { return {YomkResponse::eOk, "tempHandle result"}; };
    resp = YOMK_EVENTLOOP_POST_WAIT("workLoop", YomkMkPtr(String, "tempTask"), tempHandle);
    printResp("POST_WAIT(tempTask,tempHandle)", resp);
    printEvent(resp);

    /**
     * 步骤5：停止与续跑
     *
     * STOP 只停线程、保留队列事件；停止后投递被拒；再次 START 续跑保留事件。
     */
    printStep(
        5, "停止与续跑", "POST 两条积压任务后 STOP：线程停止但队列保留；停止后投递被拒；START 续跑消化保留事件。");
    printResp("POST(hold1)", YOMK_EVENTLOOP_POST("workLoop", YomkMkPtr(String, "hold1"), nullptr, "hold1"));
    printResp("POST(hold2)", YOMK_EVENTLOOP_POST("workLoop", YomkMkPtr(String, "hold2"), nullptr, "hold2"));
    printResp("STOP(workLoop)", YOMK_EVENTLOOP_STOP("workLoop"));
    dumpInfo("INFO_LOOP(workLoop)", YOMK_EVENTLOOP_INFO_LOOP("workLoop"));
    std::cout << ">> running:off 且积压事件仍在队列；此刻投递会被拒绝" << std::endl;
    printResp("POST(已停止)", YOMK_EVENTLOOP_POST("workLoop", YomkMkPtr(String, "rejectedTask")));
    printResp("START(workLoop,续跑)", YOMK_EVENTLOOP_START("workLoop"));
    std::cout << ">> 等待保留事件被续跑消化" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    dumpInfo("INFO_LOOP(workLoop)", YOMK_EVENTLOOP_INFO_LOOP("workLoop"));
    dumpLines("INFO_ALL", YOMK_EVENTLOOP_INFO_ALL());

    /**
     * 步骤6：销毁与 not-found
     *
     * DESTROY 停线程并清空队列、把循环移出循环表，不可续跑；
     * 对已销毁循环的一切操作均返回 eNo（not-found 惯例）。
     */
    printStep(6, "销毁与 not-found", "DESTROY 后循环表不再含 workLoop；POST/STOP/INFO_LOOP 三连操作均 eNo。");
    dumpLines("INFO_LOOPS", YOMK_EVENTLOOP_INFO_LOOPS());
    printResp("DESTROY(workLoop)", YOMK_EVENTLOOP_DESTROY("workLoop"));
    dumpLines("INFO_LOOPS(销毁后)", YOMK_EVENTLOOP_INFO_LOOPS());
    printResp("POST(已销毁)", YOMK_EVENTLOOP_POST("workLoop", YomkMkPtr(String, "any")));
    printResp("STOP(已销毁)", YOMK_EVENTLOOP_STOP("workLoop"));
    dumpInfo("INFO_LOOP(已销毁)", YOMK_EVENTLOOP_INFO_LOOP("workLoop"));

    /**
     * 步骤7：全量自省收尾
     *
     * INFO_ALL 每循环一行状态；循环表已空时输出空清单。
     */
    printStep(7, "全量自省收尾", "INFO_ALL 全量状态；循环表已空，清单为空。");
    dumpLines("INFO_ALL", YOMK_EVENTLOOP_INFO_ALL());
    std::cout << ">> 事件循环表已空" << std::endl;

    std::cout << "\n====== 示例结束：按回车退出 ======" << std::endl;
    getchar();

    return 0;
}
