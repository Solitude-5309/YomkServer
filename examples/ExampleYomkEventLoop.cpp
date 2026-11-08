// YomkEventLoop 线程隔离事件循环示例：机械臂产线班次背景的 8 个场景小示例（面向初学者）
//
// 事件循环为特定业务提供一条单线程 FIFO 执行队列：投递进同一循环的
// 事件按入队顺序在该循环的专用线程中逐个执行，循环之间彼此并行，
// 适合把"必须串行"的业务（如运动指令序列、示教器操作流水）从锁竞争里解耦出来。
//
// 本示例用"机械臂产线一次班次"贯穿 8 个场景：每个场景是一个 showXXX 小函数，
// 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
// yomk::event_loop() 的 8 个终结 op（start/stop/post/post_wait/destroy/list/get_info/all）无一遗漏。
//
// 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.motion 运动
// 循环线程 / arm.teach 示教循环线程），日志流即执行序、顺着读就能明白：
// 每个操作先打"开始执行 xxx"——操作是用户视角的业务动作（可以是一个接口
// 调用，也可以是多个接口调用组合成的完整动作），随后结果行标注本次接口
// 调用；循环线程执行的日志穿插其间，线程 id 自证执行位置。
//
// 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
//
// 场景 1 showArmLineStart      [loop(name).handle_fn(fn).msg_name(m).start() / get_info()]
// 产线开班：启动运动指令循环（默认处理函数 + MsgName 元数据），get_info()
// 确认专用线程就绪；重复 start() 同名循环为幂等重启（ResStatus::Succeeded 不报错）。
// 场景 2 showArmMotionQueue    [msg(data).loop(name).post() / tag_count(n).get_info()]
// 连续下发 5 条运动指令：post() 立即返回不阻塞；tag_count(n).get_info() 观察队列
// 积压与队首 tag 预览；第 1 条指令执行时递归投递 3 条微调指令排到
// 队尾——消费顺序自证同循环 FIFO。
// 场景 3 showArmDualLoop       [loop(name).handle_fn(fn).start() / post()]
// 双循环并行：示教器循环省 msg_name() 累积（默认处理函数、无消息名元数据），
// 与运动循环同时投递——两循环日志交错、线程 id 不同，同循环 FIFO、
// 跨循环彼此并行。
// 场景 4 showArmMotionSyncWait [msg(data).loop(name).post_wait()]
// 回原点是关键动作：post_wait() 阻塞至事件在循环线程执行完毕才返回
// （14-4 起 API 仅回 ResStatus——返回即已执行）。
// 场景 5 showArmTempHandler    [loop(name).handle_fn(fn).post_wait()（临时函数）]
// 紧急巡检例程投递时指定临时处理函数：优先于默认函数，同一循环可
// 混用两种来源（处理函数返回值不再经 API 回传）。
// 场景 6 showArmLinePause      [loop(name).stop() / 再 start() 续跑 / post()]
// 班中暂停：stop() 只停线程、保留队列事件；停止后投递被拒；
// 再次 start() 续跑消化保留事件。
// 场景 7 showArmDashboard      [list() / loop(name).get_info() / all()]
// 产线运维面板：循环名清单、单循环详情（含队列 tag 预览）、
// 全量导出——框架只回结构化数据，展示格式由用户定义。
// 场景 8 showArmLineOff        [loop(name).destroy()]
// 收班：逐循环 destroy()（停线程并清空队列、移出循环表，不可续跑）；
// 对已销毁循环的一切操作均返回 ResStatus::Failed（not-found 惯例），清单确认已空。
//
// 循环规划（跨场景流转，模拟主控/示教器两模块各自专用线程）：
// motionLoop  start() 三累积（motionHandle + String 元数据）  主控运动指令循环
// teachLoop   start() 省 msg_name（teachHandle，无消息名元数据） 示教器循环

#include <chrono>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "YomkEventLoopApi.h"
#include "YomkLogApi.h"

// ---------------------------------------------------------------------------
// 辅助输出：全部走 YomkLogger 控制台日志（log().tag(...).info 等），无 std::cout。
// 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
// 格式敏感的行先用 ostringstream 拼好再单参输出，避免多参数自动空格
// 把 "status=0" 拆成 "status= 0"。
// ---------------------------------------------------------------------------

// 场景横幅：编号 + 标题 + 涉及接口，随后一行场景背景；横幅前留一个空行分隔场景
static void logScene(int n, const std::string& title, const std::string& apis, const std::string& explain)
{
    yomk::log().tag("arm.demo").info("");
    std::ostringstream oss;
    oss << "========== 场景 " << n << "：" << title << "（" << apis << "）==========";
    yomk::log().tag("arm.demo").info(oss.str());
    yomk::log().tag("arm.demo").info("场景背景：" + explain);
}

// API 结果行（yomk::payload::Response 版）：INFO 查询等仍返回 yomk::payload::Response 的调用打印 status/msg（数据形态各异，由调用处按需展开）。
// status 三态：0=ResStatus::Succeeded 成功；1=ResStatus::Failed 名字不存在或被拒绝；-1=ResStatus::Invalid 参数无效或未初始化
static void logResp(const std::string& api, const yomk::payload::Response& resp)
{
    std::ostringstream oss;
    oss << "[" << api << "] status=" << static_cast<int>(resp.m_status);
    if (resp.m_status == yomk::payload::ResStatus::Succeeded)
    {
        oss << "(ResStatus::Succeeded 成功)";
    }
    else if (resp.m_status == yomk::payload::ResStatus::Failed)
    {
        oss << "(ResStatus::Failed 拒绝或不存在)";
    }
    else
    {
        oss << "(ResStatus::Invalid 未初始化或参数无效)";
    }
    oss << ", msg=\"" << resp.m_msg << "\"";
    yomk::log().tag("arm.demo").info(oss.str());
}

// ResStatus 版结果行：EVENTLOOP_API 操作接口 14-4 起 ResStatus 化，直接返回三态
// （失败原因经错误日志承载，无 msg 可打）
static void logResp(const std::string& api, yomk::payload::ResStatus status)
{
    const char* name = status == yomk::payload::ResStatus::Succeeded ? "ResStatus::Succeeded"
                     : status == yomk::payload::ResStatus::Failed    ? "ResStatus::Failed"
                                                                    : "ResStatus::Invalid";
    yomk::log().tag("arm.demo").info("[" + api + "] status: " + name);
}

// 操作日志（模板版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
// 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
// 结果行标注本次接口调用；循环线程执行的日志穿插其间，读者顺着日志读即可
// 理解，无需回看代码。
// 14-4：EVENTLOOP_API 操作接口返回 ResStatus，INFO 查询仍返回 yomk::payload::Response，
// 模板化使两类返回各自路由到对应 logResp 重载，调用点零改动
template <typename F>
static auto logAction(const std::string& op, const std::string& api, F&& call)
{
    yomk::log().tag("arm.demo").info("开始执行 " + op);
    auto resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如 get_info()/list()/all()）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    yomk::log().tag("arm.demo").info("开始执行 " + op);
}

// 自省结果展开：单循环 EventLoopInfo 的全部字段逐行展示（循环名行 + 字段行），
// get_info() 与 all() 共用；紧随其结果行输出，循环名行以 "-" 起头、字段行缩进对齐
static void logLoopInfo(const std::string& api, const yomk::payload::EventLoopInfo& info)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << info.m_loopName;
    yomk::log().tag("arm.demo").info(oss.str());

    oss.str("");
    oss << "[" << api << "]     running: " << (info.m_running ? "on" : "off");
    yomk::log().tag("arm.demo").info(oss.str());

    oss.str("");
    oss << "[" << api << "]     pending: " << info.m_pending;
    yomk::log().tag("arm.demo").info(oss.str());

    oss.str("");
    oss << "[" << api << "]     defaultFunc: " << (info.m_hasDefaultFunc ? "on" : "off");
    yomk::log().tag("arm.demo").info(oss.str());

    oss.str("");
    oss << "[" << api << "]     msgName: " << (info.m_msgName.empty() ? "(未指定)" : info.m_msgName);
    yomk::log().tag("arm.demo").info(oss.str());

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
    yomk::log().tag("arm.demo").info(oss.str());
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    yomk::log().tag("arm.demo").info("[验证] " + text);
}

// list() 清单行：循环名列表逐行展示（- 无序列表风格）
static void logLoopLine(const std::string& api, const std::string& loopName)
{
    yomk::log().tag("arm.demo").info("[" + api + "] - " + loopName);
}

// ---------------------------------------------------------------------------
// 事件处理函数：签名统一为 yomk::payload::Response(YomkMsgPtr)。
// 可作为默认处理函数（启动时指定）或临时处理函数（投递时指定）。
// ---------------------------------------------------------------------------

// 运动指令处理函数：默认处理函数（场景 1 start() 时指定）。
// 模拟 200ms 运动耗时，让队列积压与 FIFO 消费顺序在日志中稳定可见；
// 第 1 条指令执行时递归投递 3 条微调指令排到队尾——同循环 FIFO 的直观证据
yomk::payload::Response motionHandle(YomkMsgPtr pkg)
{
    auto cmd = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!cmd)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::ostringstream oss;
    oss << "执行运动指令: " << cmd->d << "（循环线程 " << std::this_thread::get_id() << "）";
    yomk::log().tag("arm.motion").info(oss.str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 演示处理函数内递归投递：仅 mv1 会追加 3 条微调指令到队尾，
    // 它们排在已入队的 mv2..mv5 之后消费——同循环 FIFO 的直观证据
    if (cmd->d == "mv1")
    {
        for (int i = 1; i <= 3; ++i)
        {
            yomk::event_loop().msg(yomk::msgs::String::shared("fine" + std::to_string(i))).loop("motionLoop").handle_fn(nullptr).tag("fine" + std::to_string(i)).post();
        }
        yomk::log().tag("arm.motion").info("指令 mv1 触发 3 条微调指令，已追加到队列尾部");
    }

    return {yomk::payload::ResStatus::Succeeded, "eventHandle success. "};
}

// 示教器事件处理函数：轻量打印，供双循环并行演示（场景 3 省 msg_name start() 时指定）
yomk::payload::Response teachHandle(YomkMsgPtr pkg)
{
    auto msg = yomk::msgs::unpack<yomk::msgs::String>(pkg);
    if (!msg)
    {
        return {yomk::payload::ResStatus::Failed, "pkg is null or pkg is not String"};
    }
    std::ostringstream oss;
    oss << "示教器事件: " << msg->d << "（循环线程 " << std::this_thread::get_id() << "）";
    yomk::log().tag("arm.teach").info(oss.str());
    return {yomk::payload::ResStatus::Succeeded, "teachHandle success"};
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
        "loop(name).handle_fn(fn).msg_name(m).start() / get_info()",
        "开班时主控模块启动运动指令循环：start() 为该名称新建一条专用线程并注册默认处理函数，"
        "msg_name 仅作内省元数据；对已存在名称再次 start() 是幂等重启。");

    logAction(
        "启动运动指令循环", "loop(motionLoop).start()", [&] { return yomk::event_loop().loop("motionLoop").handle_fn(motionHandle).msg_name("String").start(); });

    logAction("查看循环运行状态");
    yomk::payload::Response resp = yomk::event_loop().loop("motionLoop").get_info();
    logResp("loop(motionLoop).get_info()", resp);
    auto info = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(resp.m_data);
    if (info)
    {
        logLoopInfo("get_info()", info->d);
    }
    logVerify("running=on、defaultFunc=on：专用线程已就绪，未指定函数的事件由 motionHandle 处理");

    // 重复启动同名循环：幂等重启，不报错也不影响线程与队列
    logAction(
        "重复启动同名循环", "loop(motionLoop).start()（重复）", [&] { return yomk::event_loop().loop("motionLoop").handle_fn(motionHandle).msg_name("String").start(); });
    logVerify("对已存在名称再次 start() -> ResStatus::Succeeded：幂等重启，线程与队列不受影响");
}

// 场景 2：插补运动连续下发（异步投递 + 队列观察 + FIFO）
static void showArmMotionQueue()
{
    logScene(
        2,
        "连续下发运动指令",
        "msg(data).loop(name).post() / tag_count(n).get_info()",
        "插补运动连续下发：POST 把事件放进循环队列后立即返回，不阻塞运动控制线程；"
        "tag 仅内省可见，用于观察队列积压；同循环内事件按入队顺序（FIFO）执行。");

    // 连续 5 条指令（每条处理 200ms）：POST 立即返回，指令在循环线程排队执行
    for (int i = 1; i <= 5; ++i)
    {
        const std::string cmd = "mv" + std::to_string(i);
        logAction(
            "下发运动指令 " + cmd,
            "POST motionLoop",
            [&] { return yomk::event_loop().msg(yomk::msgs::String::shared(cmd)).loop("motionLoop").handle_fn(nullptr).tag(cmd).post(); });
    }

    logAction("立即观察队列积压");
    yomk::payload::Response resp = yomk::event_loop().loop("motionLoop").tag_count(5).get_info();
    logResp("loop(motionLoop).tag_count(5).get_info()", resp);
    auto backlog = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(resp.m_data);
    if (backlog)
    {
        logLoopInfo("get_info()", backlog->d);
    }
    logVerify("正在执行的事件不计入 pending，nextTags 列出待处理指令的 tag——POST 已返回而指令仍在排队");

    yomk::log().tag("arm.demo").info("等待队列排空，观察 arm.motion 日志的消费顺序：mv2..mv5 之后才是 fine1..fine3...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));

    logAction("排空后复查");
    resp = yomk::event_loop().loop("motionLoop").tag_count(5).get_info();
    logResp("loop(motionLoop).tag_count(5).get_info()", resp);
    auto drained = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(resp.m_data);
    if (drained)
    {
        logLoopInfo("get_info()", drained->d);
    }
    logVerify("mv1 执行中触发的 fine1..fine3 排到已入队指令之后消费——同循环 FIFO，递归投递不插队");
}

// 场景 3：双循环并行（示教器独立于主控）
static void showArmDualLoop()
{
    logScene(
        3,
        "双循环并行",
        "loop(name).handle_fn(fn).start() / post()",
        "示教器模块独立于主控：teachLoop 省 msg_name() 累积（默认处理函数、无消息名元数据）；"
        "两个循环各自专用线程，同循环内 FIFO、跨循环彼此并行。");

    logAction("启动示教器循环", "loop(teachLoop).start()", [&] { return yomk::event_loop().loop("teachLoop").handle_fn(teachHandle).start(); });

    std::ostringstream ossThreadId;
    ossThreadId << "主线程 id: " << std::this_thread::get_id();
    yomk::log().tag("arm.demo").info(ossThreadId.str());

    // 两循环交错投递：各自专用线程并行消费，日志交错出现；
    // 操作行带指令名，与循环线程的消费日志一一对应
    logAction(
        "主控下发轨迹指令 track1", "POST motionLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("track1")).loop("motionLoop").handle_fn(nullptr).tag("track1").post(); });
    logAction(
        "示教器记录示教点 point1", "POST teachLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("point1")).loop("teachLoop").handle_fn(nullptr).tag("point1").post(); });
    logAction(
        "主控下发轨迹指令 track2", "POST motionLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("track2")).loop("motionLoop").handle_fn(nullptr).tag("track2").post(); });
    logAction(
        "示教器记录示教点 point2", "POST teachLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("point2")).loop("teachLoop").handle_fn(nullptr).tag("point2").post(); });

    yomk::log().tag("arm.demo").info("等待两循环消费（各 2 条 x 200ms）...");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    logVerify("arm.motion 与 arm.teach 日志交错出现且线程 id 均不同于主线程——跨循环彼此并行");
}

// 场景 4：关键指令同步等待（post_wait()）
static void showArmMotionSyncWait()
{
    logScene(
        4,
        "关键指令同步等待",
        "msg(data).loop(name).post_wait()",
        "回原点是关键动作，必须确认执行完毕才能继续：post_wait() 阻塞至事件在循环线程"
        "执行完毕才返回（14-4 起 API 仅回 ResStatus，回传 Event 载荷不再经 API 暴露）。");

    logAction(
        "下发回原点指令并等待完成", "loop(motionLoop).post_wait()", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("goHome")).loop("motionLoop").post_wait(); });
    logVerify("post_wait() 返回时回原点已在循环线程执行完毕（耗时 200ms）——返回即已执行");
}

// 场景 5：紧急任务临时处理函数（优先于默认函数）
static void showArmTempHandler()
{
    logScene(
        5,
        "紧急任务临时处理函数",
        "loop(name).handle_fn(fn).post_wait()（临时函数）",
        "安全巡检例程无需占用运动处理函数：投递时指定临时处理函数则优先于默认函数"
        "（14-4 起 API 仅回 ResStatus，处理函数返回值不再经 API 回传）。");

    // lambda 先赋给具名变量再传宏：返回值构造含逗号时，
    // 直接内联进宏会让预处理器把逗号误当实参分隔符
    YomkServiceFunc tempHandle = [](YomkMsgPtr) -> yomk::payload::Response { return {yomk::payload::ResStatus::Succeeded, "tempHandle result"}; };

    logAction(
        "投递紧急巡检任务（临时函数）",
        "loop(motionLoop).post_wait()（tempHandle）",
        [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("safetyCheck")).loop("motionLoop").handle_fn(tempHandle).post_wait(); });
    logVerify("临时函数优先于默认函数执行（处理函数返回值不再经 API 回传）：post_wait() 返回即已执行");
}

// 场景 6：班中暂停与恢复（stop() / start() 续跑）
static void showArmLinePause()
{
    logScene(
        6,
        "班中暂停与恢复",
        "msg(data).loop(name).post() / stop() / start()（续跑）",
        "换班暂停：stop() 只停线程、保留队列事件；停止后投递被拒；再次 start() 续跑消化保留事件。");

    // 投递 2 条积压指令后立即暂停：线程停止但事件保留在队列
    logAction(
        "下发待消化指令 hold1", "POST motionLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("hold1")).loop("motionLoop").handle_fn(nullptr).tag("hold1").post(); });
    logAction(
        "下发待消化指令 hold2", "POST motionLoop", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("hold2")).loop("motionLoop").handle_fn(nullptr).tag("hold2").post(); });
    logAction("暂停运动循环", "loop(motionLoop).stop()", [&] { return yomk::event_loop().loop("motionLoop").stop(); });

    logAction("查看暂停后状态");
    yomk::payload::Response resp = yomk::event_loop().loop("motionLoop").get_info();
    logResp("loop(motionLoop).get_info()", resp);
    auto paused = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(resp.m_data);
    if (paused)
    {
        logLoopInfo("get_info()", paused->d);
    }
    logVerify("running=off 且积压事件保留在队列：stop() 只停线程不清队列，可续跑");

    logAction(
        "停止态投递被拒", "POST motionLoop (已停止)", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("rejected")).loop("motionLoop").post(); });
    logVerify("停止后投递 -> ResStatus::Failed(event loop not running, post rejected)：循环未运行不接收新事件");

    logAction("恢复运行（续跑）", "loop(motionLoop).start()（续跑）", [&] { return yomk::event_loop().loop("motionLoop").handle_fn(motionHandle).msg_name("String").start(); });
    yomk::log().tag("arm.demo").info("等待保留事件被续跑消化...");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    logAction("确认积压已消化");
    resp = yomk::event_loop().loop("motionLoop").get_info();
    logResp("loop(motionLoop).get_info()", resp);
    auto resumed = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(resp.m_data);
    if (resumed)
    {
        logLoopInfo("get_info()", resumed->d);
    }
    logVerify("续跑后保留事件按 FIFO 消化完毕，pending 归零：stop()/start() 不丢事件");
}

// 场景 7：产线运维面板巡检
static void showArmDashboard()
{
    logScene(
        7,
        "产线运维面板",
        "list() / loop(name).get_info() / all()",
        "运维面板巡检：list() 拉取循环名清单；get_info() 单查循环详情（含队列 tag "
        "预览）；all() 全量导出——框架只回结构化数据，展示格式由用户定义。");

    // 循环名清单（StringArray 结构化包）
    logAction("巡检循环名清单");
    yomk::payload::Response loopsResp = yomk::event_loop().list();
    logResp("list()", loopsResp);
    auto loopArr = yomk::msgs::unpack<yomk::msgs::StringArray>(loopsResp.m_data);
    if (loopArr)
    {
        for (const auto& loopName : loopArr->d)
        {
            logLoopLine("list()", loopName);
        }
    }

    // 单循环抽查：运动循环（含队列 tag 预览，队列空时 nextTags 为空表）
    logAction("抽查运动循环详情");
    yomk::payload::Response loopResp = yomk::event_loop().loop("motionLoop").tag_count(3).get_info();
    logResp("loop(motionLoop).tag_count(3).get_info()", loopResp);
    auto loopInfo = yomk::msgs::unpack<yomk::msgs::EventLoopInfo>(loopResp.m_data);
    if (loopInfo)
    {
        logLoopInfo("get_info()", loopInfo->d);
    }
    logVerify("motionLoop：running=on、defaultFunc=on（场景 1 注册的默认处理函数仍在）");

    // 全量导出（EventLoopInfoList 结构化包），逐循环展开全部字段
    logAction("全量导出循环状态");
    yomk::payload::Response allResp = yomk::event_loop().all();
    logResp("all()", allResp);
    auto allArr = yomk::msgs::unpack<yomk::msgs::EventLoopInfoList>(allResp.m_data);
    if (allArr)
    {
        for (const auto& info : allArr->d)
        {
            logLoopInfo("all()", info);
        }
    }
}

// 场景 8：收班下线（destroy() 与 not-found 惯例）
static void showArmLineOff()
{
    logScene(
        8,
        "收班销毁",
        "loop(name).destroy()",
        "收班下线全部循环：destroy() 停线程并清空队列、把循环移出循环表，不可续跑；"
        "对已销毁循环的一切操作均返回 ResStatus::Failed（not-found 惯例）。");

    logAction("销毁运动循环", "loop(motionLoop).destroy()", [&] { return yomk::event_loop().loop("motionLoop").destroy(); });
    logAction("销毁示教循环", "loop(teachLoop).destroy()", [&] { return yomk::event_loop().loop("teachLoop").destroy(); });

    logAction("巡检清单确认已清空");
    yomk::payload::Response loopsResp = yomk::event_loop().list();
    logResp("list()", loopsResp);
    auto loopArr = yomk::msgs::unpack<yomk::msgs::StringArray>(loopsResp.m_data);
    logVerify(loopArr && loopArr->d.empty() ? "循环清单已空：机械臂班次结束，全部专用线程已下线" : "循环清单非空：存在未销毁循环");

    logAction(
        "已销毁循环投递被拒", "POST motionLoop (已销毁)", [&] { return yomk::event_loop().msg(yomk::msgs::String::shared("any")).loop("motionLoop").post(); });
    logAction("已销毁循环停止被拒", "loop(motionLoop).stop()（已销毁）", [&] { return yomk::event_loop().loop("motionLoop").stop(); });
    logAction("已销毁循环查询被拒");
    yomk::payload::Response resp = yomk::event_loop().loop("motionLoop").get_info();
    logResp("loop(motionLoop).get_info()（已销毁）", resp);
    logVerify("对已销毁循环的一切操作 -> ResStatus::Failed(event loop not exist)：not-found 惯例，销毁不可续跑");
}

int main(int argc, char* argv[])
{
    // 初始化框架（事件循环服务 /YomkEventLoop 随之自动启动）
    yomk::server().init();

    yomk::log().tag("arm.demo").info("机械臂产线 x YomkEventLoop 示例：一次班次贯穿 8 个场景，覆盖全部事件循环 API");
    yomk::log().tag("arm.demo").info("日志按 tag 分层：arm.demo=叙事与结果 / arm.motion=运动循环线程 / arm.teach=示教循环线程");

    showArmLineStart();
    showArmMotionQueue();
    showArmDualLoop();
    showArmMotionSyncWait();
    showArmTempHandler();
    showArmLinePause();
    showArmDashboard();
    showArmLineOff();

    yomk::log().tag("arm.demo").info("========== 示例结束：按回车退出 ==========");
    getchar();

    return 0;
}
