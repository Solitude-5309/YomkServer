/**
 * @file ExampleYomkEventLoop.cpp
 * @brief YomkEventLoop 线程隔离事件循环示例：机械臂产线班次背景的 8 个场景小示例（面向初学者）
 *
 * 事件循环为特定业务提供一条单线程 FIFO 执行队列：投递进同一循环的
 * 事件按入队顺序在该循环的专用线程中逐个执行，循环之间彼此并行，
 * 适合把"必须串行"的业务（如运动指令序列、示教器操作流水）从锁竞争里解耦出来。
 *
 * 本示例用"机械臂产线一次班次"贯穿 8 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
 * 全部 8 个 YOMK_EVENTLOOP_* 用户接口无一遗漏。
 *
 * 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.motion 运动
 * 循环线程 / arm.teach 示教循环线程），日志流即执行序、顺着读就能明白：
 * 每个操作先打"开始执行 xxx"——操作是用户视角的业务动作（可以是一个接口
 * 调用，也可以是多个接口调用组合成的完整动作），随后结果行标注本次接口
 * 调用；循环线程执行的日志穿插其间，线程 id 自证执行位置。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmLineStart      [YOMK_EVENTLOOP_START(3参) / INFO_LOOP]
 *        产线开班：启动运动指令循环（默认处理函数 + MsgName 元数据），INFO_LOOP
 *        确认专用线程就绪；重复 START 同名循环为幂等重启（eOk 不报错）。
 * 场景 2 showArmMotionQueue    [YOMK_EVENTLOOP_POST / INFO_LOOP(n)]
 *        连续下发 5 条运动指令：POST 立即返回不阻塞；INFO_LOOP(n) 观察队列
 *        积压与队首 tag 预览；第 1 条指令执行时递归投递 3 条微调指令排到
 *        队尾——消费顺序自证同循环 FIFO。
 * 场景 3 showArmDualLoop       [YOMK_EVENTLOOP_START(2参) / POST]
 *        双循环并行：示教器循环用 2 参 START（默认处理函数、无 MsgName 元数据），
 *        与运动循环同时投递——两循环日志交错、线程 id 不同，同循环 FIFO、
 *        跨循环彼此并行。
 * 场景 4 showArmMotionSyncWait [YOMK_EVENTLOOP_POST_WAIT]
 *        回原点是关键动作：POST_WAIT 阻塞至事件在循环线程执行完毕才返回，
 *        Event 包三要素回传（eventId 单调递增 / loopName / 处理函数返回值）。
 * 场景 5 showArmTempHandler    [YOMK_EVENTLOOP_POST_WAIT(临时函数)]
 *        紧急巡检例程投递时指定临时处理函数：优先于默认函数，返回临时函数
 *        的结果——同一循环可混用两种来源。
 * 场景 6 showArmLinePause      [YOMK_EVENTLOOP_STOP / START(续跑) / POST]
 *        班中暂停：STOP 只停线程、保留队列事件；停止后投递被拒；
 *        再次 START 续跑消化保留事件。
 * 场景 7 showArmDashboard      [INFO_LOOPS / INFO_LOOP / INFO_ALL]
 *        产线运维面板：循环名清单、单循环详情（含队列 tag 预览）、
 *        全量导出——框架只回结构化数据，展示格式由用户定义。
 * 场景 8 showArmLineOff        [YOMK_EVENTLOOP_DESTROY]
 *        收班：逐循环 DESTROY（停线程并清空队列、移出循环表，不可续跑）；
 *        对已销毁循环的一切操作均返回 eNo（not-found 惯例），清单确认已空。
 *
 * 循环规划（跨场景流转，模拟主控/示教器两模块各自专用线程）：
 *   motionLoop  START 3 参（motionHandle + String 元数据）  主控运动指令循环
 *   teachLoop   START 2 参（teachHandle，无 MsgName 元数据） 示教器循环
 */

#include <chrono>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "YomkAPI.h"

// ---------------------------------------------------------------------------
// 辅助输出：全部走 YomkLogger 控制台日志（YOMK_INFO_TAG 等），无 std::cout。
// 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
// 格式敏感的行先用 ostringstream 拼好再单参输出，避免多参数自动空格
// 把 "status=0" 拆成 "status= 0"。
// ---------------------------------------------------------------------------

// 场景横幅：编号 + 标题 + 涉及接口，随后一行场景背景；横幅前留一个空行分隔场景
static void logScene(int n, const std::string& title, const std::string& apis, const std::string& explain)
{
    YOMK_INFO_TAG("arm.demo", "");
    std::ostringstream oss;
    oss << "========== 场景 " << n << "：" << title << "（" << apis << "）==========";
    YOMK_INFO_TAG("arm.demo", oss.str());
    YOMK_INFO_TAG("arm.demo", "场景背景：" + explain);
}

// API 结果行：打印 YomkResponse 三要素中的 status/msg（数据形态各异，由调用处按需展开）。
// status 三态：0=eOk 成功；1=eNo 名字不存在或被拒绝；-1=eInvalid 参数无效或未初始化
static void logResp(const std::string& api, const YomkResponse& resp)
{
    std::ostringstream oss;
    oss << "[" << api << "] status=" << resp.m_status;
    if (resp.m_status == YomkResponse::eOk)
    {
        oss << "(eOk 成功)";
    }
    else if (resp.m_status == YomkResponse::eNo)
    {
        oss << "(eNo 拒绝或不存在)";
    }
    else
    {
        oss << "(eInvalid 未初始化或参数无效)";
    }
    oss << ", msg=\"" << resp.m_msg << "\"";
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 操作日志（YomkResponse 版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
// 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
// 结果行标注本次接口调用；循环线程执行的日志穿插其间，读者顺着日志读即可
// 理解，无需回看代码
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如 INFO_LOOP/INFO_LOOPS/INFO_ALL）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// 自省结果展开：单循环 EventLoopInfo 的全部字段逐行展示（循环名行 + 字段行），
// INFO_LOOP 与 INFO_ALL 共用；紧随其结果行输出，循环名行以 "-" 起头、字段行缩进对齐
static void logLoopInfo(const std::string& api, const yomk::EventLoopInfo& info)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << info.m_loopName;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     running: " << (info.m_running ? "on" : "off");
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     pending: " << info.m_pending;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     defaultFunc: " << (info.m_hasDefaultFunc ? "on" : "off");
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     msgName: " << (info.m_msgName.empty() ? "(未指定)" : info.m_msgName);
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     nextTags: [";
    for (size_t i = 0; i < info.m_nextTags.size(); ++i)
    {
        if (i != 0)
        {
            oss << ", ";
        }
        oss << (info.m_nextTags[i].empty() ? "-" : info.m_nextTags[i]);
    }
    oss << "]";
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// POST_WAIT 回传的 Event 包逐字段展开：eventId / loopName / response（处理函数返回值）
static void logEvent(const YomkResponse& resp)
{
    YomkUnPackPkg(resp.m_data, Event, event);
    if (!event)
    {
        return;
    }
    std::ostringstream oss;
    oss << "[Event] - eventId: " << event->d.m_eventId;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[Event]     loopName: " << event->d.m_eventLoopName;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[Event]     response: status=" << event->d.m_response.m_status << ", msg=\"" << event->d.m_response.m_msg
        << "\"";
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    YOMK_INFO_TAG("arm.demo", "[验证] " + text);
}

// INFO_LOOPS 清单行：循环名列表逐行展示（- 无序列表风格）
static void logLoopLine(const std::string& api, const std::string& loopName)
{
    YOMK_INFO_TAG("arm.demo", "[" + api + "] - " + loopName);
}

// ---------------------------------------------------------------------------
// 事件处理函数：签名统一为 YomkResponse(YomkPkgPtr)。
// 可作为默认处理函数（启动时指定）或临时处理函数（投递时指定）。
// ---------------------------------------------------------------------------

// 运动指令处理函数：默认处理函数（场景 1 START 时指定）。
// 模拟 200ms 运动耗时，让队列积压与 FIFO 消费顺序在日志中稳定可见；
// 第 1 条指令执行时递归投递 3 条微调指令排到队尾——同循环 FIFO 的直观证据
YomkResponse motionHandle(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, cmd);
    std::ostringstream oss;
    oss << "执行运动指令: " << cmd->d << "（循环线程 " << std::this_thread::get_id() << "）";
    YOMK_INFO_TAG("arm.motion", oss.str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 演示处理函数内递归投递：仅 mv1 会追加 3 条微调指令到队尾，
    // 它们排在已入队的 mv2..mv5 之后消费——同循环 FIFO 的直观证据
    if (cmd->d == "mv1")
    {
        for (int i = 1; i <= 3; ++i)
        {
            YOMK_EVENTLOOP_POST(
                "motionLoop", YomkMkPtr(String, "fine" + std::to_string(i)), nullptr, "fine" + std::to_string(i));
        }
        YOMK_INFO_TAG("arm.motion", "指令 mv1 触发 3 条微调指令，已追加到队列尾部");
    }

    return {YomkResponse::eOk, "eventHandle success. "};
}

// 示教器事件处理函数：轻量打印，供双循环并行演示（场景 3 START 2 参时指定）
YomkResponse teachHandle(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, msg);
    std::ostringstream oss;
    oss << "示教器事件: " << msg->d << "（循环线程 " << std::this_thread::get_id() << "）";
    YOMK_INFO_TAG("arm.teach", oss.str());
    return {YomkResponse::eOk, "teachHandle success"};
}

// ---------------------------------------------------------------------------
// 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
// 循环流转：motionLoop 场景 1 创建、teachLoop 场景 3 创建，场景 8 一并销毁。
// ---------------------------------------------------------------------------

// 场景 1：产线开班，主控模块启动运动指令循环
static void showArmLineStart()
{
    logScene(
        1,
        "产线开班启动运动循环",
        "YOMK_EVENTLOOP_START(3参) / INFO_LOOP",
        "开班时主控模块启动运动指令循环：START 为该名称新建一条专用线程并注册默认处理函数，"
        "MsgName 仅作内省元数据；对已存在名称再次 START 是幂等重启。");

    logAction(
        "启动运动指令循环", "START motionLoop", [&] { return YOMK_EVENTLOOP_START("motionLoop", motionHandle, String); });

    logAction("查看循环运行状态");
    YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop");
    logResp("INFO_LOOP motionLoop", resp);
    YomkUnPackPkg(resp.m_data, EventLoopInfo, info);
    if (info)
    {
        logLoopInfo("INFO_LOOP", info->d);
    }
    logVerify("running=on、defaultFunc=on：专用线程已就绪，未指定函数的事件由 motionHandle 处理");

    // 重复启动同名循环：幂等重启，不报错也不影响线程与队列
    logAction(
        "重复启动同名循环", "START motionLoop (重复)", [&] { return YOMK_EVENTLOOP_START("motionLoop", motionHandle, String); });
    logVerify("对已存在名称再次 START -> eOk：幂等重启，线程与队列不受影响");
}

// 场景 2：插补运动连续下发（异步投递 + 队列观察 + FIFO）
static void showArmMotionQueue()
{
    logScene(
        2,
        "连续下发运动指令",
        "YOMK_EVENTLOOP_POST / INFO_LOOP(n)",
        "插补运动连续下发：POST 把事件放进循环队列后立即返回，不阻塞运动控制线程；"
        "tag 仅内省可见，用于观察队列积压；同循环内事件按入队顺序（FIFO）执行。");

    // 连续 5 条指令（每条处理 200ms）：POST 立即返回，指令在循环线程排队执行
    for (int i = 1; i <= 5; ++i)
    {
        const std::string cmd = "mv" + std::to_string(i);
        logAction(
            "下发运动指令 " + cmd,
            "POST motionLoop",
            [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, cmd), nullptr, cmd); });
    }

    logAction("立即观察队列积压");
    YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop", 5);
    logResp("INFO_LOOP motionLoop(5)", resp);
    YomkUnPackPkg(resp.m_data, EventLoopInfo, backlog);
    if (backlog)
    {
        logLoopInfo("INFO_LOOP", backlog->d);
    }
    logVerify("正在执行的事件不计入 pending，nextTags 列出待处理指令的 tag——POST 已返回而指令仍在排队");

    YOMK_INFO_TAG("arm.demo", "等待队列排空，观察 arm.motion 日志的消费顺序：mv2..mv5 之后才是 fine1..fine3...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));

    logAction("排空后复查");
    resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop", 5);
    logResp("INFO_LOOP motionLoop(5)", resp);
    YomkUnPackPkg(resp.m_data, EventLoopInfo, drained);
    if (drained)
    {
        logLoopInfo("INFO_LOOP", drained->d);
    }
    logVerify("mv1 执行中触发的 fine1..fine3 排到已入队指令之后消费——同循环 FIFO，递归投递不插队");
}

// 场景 3：双循环并行（示教器独立于主控）
static void showArmDualLoop()
{
    logScene(
        3,
        "双循环并行",
        "YOMK_EVENTLOOP_START(2参) / POST",
        "示教器模块独立于主控：teachLoop 用 2 参 START（默认处理函数、无 MsgName 元数据）；"
        "两个循环各自专用线程，同循环内 FIFO、跨循环彼此并行。");

    logAction("启动示教器循环", "START teachLoop", [&] { return YOMK_EVENTLOOP_START("teachLoop", teachHandle); });

    std::ostringstream ossThreadId;
    ossThreadId << "主线程 id: " << std::this_thread::get_id();
    YOMK_INFO_TAG("arm.demo", ossThreadId.str());

    // 两循环交错投递：各自专用线程并行消费，日志交错出现；
    // 操作行带指令名，与循环线程的消费日志一一对应
    logAction(
        "主控下发轨迹指令 track1", "POST motionLoop", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "track1"), nullptr, "track1"); });
    logAction(
        "示教器记录示教点 point1", "POST teachLoop", [&] { return YOMK_EVENTLOOP_POST("teachLoop", YomkMkPtr(String, "point1"), nullptr, "point1"); });
    logAction(
        "主控下发轨迹指令 track2", "POST motionLoop", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "track2"), nullptr, "track2"); });
    logAction(
        "示教器记录示教点 point2", "POST teachLoop", [&] { return YOMK_EVENTLOOP_POST("teachLoop", YomkMkPtr(String, "point2"), nullptr, "point2"); });

    YOMK_INFO_TAG("arm.demo", "等待两循环消费（各 2 条 x 200ms）...");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    logVerify("arm.motion 与 arm.teach 日志交错出现且线程 id 均不同于主线程——跨循环彼此并行");
}

// 场景 4：关键指令同步等待（POST_WAIT）
static void showArmMotionSyncWait()
{
    logScene(
        4,
        "关键指令同步等待",
        "YOMK_EVENTLOOP_POST_WAIT",
        "回原点是关键动作，必须确认执行完毕才能继续：POST_WAIT 阻塞至事件在循环线程"
        "执行完毕，回传 Event 包三要素（eventId / loopName / 处理函数返回值）。");

    YomkResponse resp = logAction(
        "下发回原点指令并等待完成", "POST_WAIT motionLoop", [&] { return YOMK_EVENTLOOP_POST_WAIT("motionLoop", YomkMkPtr(String, "goHome")); });
    logEvent(resp);
    logVerify("POST_WAIT 返回时回原点已在循环线程执行完毕（耗时 200ms），Event 包携带执行结果");
}

// 场景 5：紧急任务临时处理函数（优先于默认函数）
static void showArmTempHandler()
{
    logScene(
        5,
        "紧急任务临时处理函数",
        "YOMK_EVENTLOOP_POST_WAIT(临时函数)",
        "安全巡检例程无需占用运动处理函数：投递时指定临时处理函数则优先于默认函数，"
        "返回临时函数的结果——同一循环可混用两种来源。");

    // lambda 先赋给具名变量再传宏：返回值构造含逗号时，
    // 直接内联进宏会让预处理器把逗号误当实参分隔符
    YomkServiceFunc tempHandle = [](YomkPkgPtr) -> YomkResponse { return {YomkResponse::eOk, "tempHandle result"}; };

    YomkResponse resp = logAction(
        "投递紧急巡检任务（临时函数）",
        "POST_WAIT motionLoop (tempHandle)",
        [&] { return YOMK_EVENTLOOP_POST_WAIT("motionLoop", YomkMkPtr(String, "safetyCheck"), tempHandle); });
    logEvent(resp);
    logVerify("response 为临时函数的结果（tempHandle result）而非默认函数结果：临时函数优先于默认函数");
}

// 场景 6：班中暂停与恢复（STOP / START 续跑）
static void showArmLinePause()
{
    logScene(
        6,
        "班中暂停与恢复",
        "YOMK_EVENTLOOP_POST / STOP / START(续跑)",
        "换班暂停：STOP 只停线程、保留队列事件；停止后投递被拒；再次 START 续跑消化保留事件。");

    // 投递 2 条积压指令后立即暂停：线程停止但事件保留在队列
    logAction(
        "下发待消化指令 hold1", "POST motionLoop", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "hold1"), nullptr, "hold1"); });
    logAction(
        "下发待消化指令 hold2", "POST motionLoop", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "hold2"), nullptr, "hold2"); });
    logAction("暂停运动循环", "STOP motionLoop", [&] { return YOMK_EVENTLOOP_STOP("motionLoop"); });

    logAction("查看暂停后状态");
    YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop");
    logResp("INFO_LOOP motionLoop", resp);
    YomkUnPackPkg(resp.m_data, EventLoopInfo, paused);
    if (paused)
    {
        logLoopInfo("INFO_LOOP", paused->d);
    }
    logVerify("running=off 且积压事件保留在队列：STOP 只停线程不清队列，可续跑");

    logAction(
        "停止态投递被拒", "POST motionLoop (已停止)", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "rejected")); });
    logVerify("停止后投递 -> eNo(event loop not running, post rejected)：循环未运行不接收新事件");

    logAction("恢复运行（续跑）", "START motionLoop (续跑)", [&] { return YOMK_EVENTLOOP_START("motionLoop", motionHandle, String); });
    YOMK_INFO_TAG("arm.demo", "等待保留事件被续跑消化...");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    logAction("确认积压已消化");
    resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop");
    logResp("INFO_LOOP motionLoop", resp);
    YomkUnPackPkg(resp.m_data, EventLoopInfo, resumed);
    if (resumed)
    {
        logLoopInfo("INFO_LOOP", resumed->d);
    }
    logVerify("续跑后保留事件按 FIFO 消化完毕，pending 归零：STOP/START 不丢事件");
}

// 场景 7：产线运维面板巡检
static void showArmDashboard()
{
    logScene(
        7,
        "产线运维面板",
        "INFO_LOOPS / INFO_LOOP / INFO_ALL",
        "运维面板巡检：INFO_LOOPS 拉取循环名清单；INFO_LOOP 单查循环详情（含队列 tag "
        "预览）；INFO_ALL 全量导出——框架只回结构化数据，展示格式由用户定义。");

    // 循环名清单（StringArray 结构化包）
    logAction("巡检循环名清单");
    YomkResponse loopsResp = YOMK_EVENTLOOP_INFO_LOOPS();
    logResp("INFO_LOOPS", loopsResp);
    YomkUnPackPkg(loopsResp.m_data, StringArray, loopArr);
    if (loopArr)
    {
        for (const auto& loopName : loopArr->d)
        {
            logLoopLine("INFO_LOOPS", loopName);
        }
    }

    // 单循环抽查：运动循环（含队列 tag 预览，队列空时 nextTags 为空表）
    logAction("抽查运动循环详情");
    YomkResponse loopResp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop", 3);
    logResp("INFO_LOOP motionLoop(3)", loopResp);
    YomkUnPackPkg(loopResp.m_data, EventLoopInfo, loopInfo);
    if (loopInfo)
    {
        logLoopInfo("INFO_LOOP", loopInfo->d);
    }
    logVerify("motionLoop：running=on、defaultFunc=on（场景 1 注册的默认处理函数仍在）");

    // 全量导出（EventLoopInfoList 结构化包），逐循环展开全部字段
    logAction("全量导出循环状态");
    YomkResponse allResp = YOMK_EVENTLOOP_INFO_ALL();
    logResp("INFO_ALL", allResp);
    YomkUnPackPkg(allResp.m_data, EventLoopInfoList, allArr);
    if (allArr)
    {
        for (const auto& info : allArr->d)
        {
            logLoopInfo("INFO_ALL", info);
        }
    }
}

// 场景 8：收班下线（DESTROY 与 not-found 惯例）
static void showArmLineOff()
{
    logScene(
        8,
        "收班销毁",
        "YOMK_EVENTLOOP_DESTROY",
        "收班下线全部循环：DESTROY 停线程并清空队列、把循环移出循环表，不可续跑；"
        "对已销毁循环的一切操作均返回 eNo（not-found 惯例）。");

    logAction("销毁运动循环", "DESTROY motionLoop", [&] { return YOMK_EVENTLOOP_DESTROY("motionLoop"); });
    logAction("销毁示教循环", "DESTROY teachLoop", [&] { return YOMK_EVENTLOOP_DESTROY("teachLoop"); });

    logAction("巡检清单确认已清空");
    YomkResponse loopsResp = YOMK_EVENTLOOP_INFO_LOOPS();
    logResp("INFO_LOOPS", loopsResp);
    YomkUnPackPkg(loopsResp.m_data, StringArray, loopArr);
    logVerify(loopArr && loopArr->d.empty() ? "循环清单已空：机械臂班次结束，全部专用线程已下线" : "循环清单非空：存在未销毁循环");

    logAction(
        "已销毁循环投递被拒", "POST motionLoop (已销毁)", [&] { return YOMK_EVENTLOOP_POST("motionLoop", YomkMkPtr(String, "any")); });
    logAction("已销毁循环停止被拒", "STOP motionLoop (已销毁)", [&] { return YOMK_EVENTLOOP_STOP("motionLoop"); });
    logAction("已销毁循环查询被拒");
    YomkResponse resp = YOMK_EVENTLOOP_INFO_LOOP("motionLoop");
    logResp("INFO_LOOP motionLoop (已销毁)", resp);
    logVerify("对已销毁循环的一切操作 -> eNo(event loop not exist)：not-found 惯例，销毁不可续跑");
}

int main(int argc, char* argv[])
{
    // 初始化框架（事件循环服务 /YomkEventLoop 随之自动启动）
    YOMK_INIT();

    YOMK_INFO_TAG("arm.demo", "机械臂产线 x YomkEventLoop 示例：一次班次贯穿 8 个场景，覆盖全部事件循环 API");
    YOMK_INFO_TAG("arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.motion=运动循环线程 / arm.teach=示教循环线程");

    showArmLineStart();
    showArmMotionQueue();
    showArmDualLoop();
    showArmMotionSyncWait();
    showArmTempHandler();
    showArmLinePause();
    showArmDashboard();
    showArmLineOff();

    YOMK_INFO_TAG("arm.demo", "========== 示例结束：按回车退出 ==========");
    getchar();

    return 0;
}
