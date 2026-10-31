/**
 * @file ExampleYomkContext.cpp
 * @brief YomkContext 共享上下文示例：机械臂控制背景的 8 个场景小示例（面向初学者）
 *
 * 上下文是全局共享的强类型键值状态机：把消息包按 key 存起来，任意位置按 key
 * 读写——机械臂系统中主控、示教器、安全 PLC、数字孪生正是这样共享状态。
 * 在此之上还有两道可选防线：变更前校验门控（Checker，返回 eReject 可拦截非法
 * 写入）与变更后通知（Monitor，写入成功后收到键值快照，同步/异步可选）。
 *
 * 本示例用"机械臂一次作业会话"贯穿 8 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
 * 全部 13 个 YomkAPI::CONTEXT_* 用户接口无一遗漏。
 *
 * 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.guard 校验 /
 * arm.watch 通知 / arm.telemetry 上报），日志流即执行序、顺着读就能明白：
 * 每个操作先打"开始执行 xxx"——操作是用户视角的业务动作（可以是一个接口
 * 调用，也可以是多个接口调用组合成的完整动作），随后结果行标注本次接口
 * 调用；同步 checker/monitor 回调插入的日志被夹在两者之间，归属一目了然；
 * 异步通知则在结果行之后送达。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmBootCreate      [YomkAPI::createContext]
 *        控制柜上电，主控模块写入初始状态（模式/速度倍率/伺服状态）；
 *        重复创建同名键被拒——创建是排他的，更新请用 SET。
 * 场景 2 showArmReadStatus      [YomkAPI::getContext]
 *        示教器读速度倍率命中只读快照；查询未上线负载模块的键返回兜底
 *        默认值（指针相等自证兜底路径，调用方永不拿到空指针）。
 * 场景 3 showArmUpdateStatus    [YomkAPI::setContext]
 *        切自动模式并提速；误用 Float64 改 String 键被拒（强类型防误写）；
 *        SET 整体替换值对象——先前持有者冻结旧快照，无撕裂读。
 * 场景 4 showArmGuardSpeedRatio [YomkAPI::setContextChecker / ON_CHECKER / OFF_CHECKER]
 *        倍率超 1.0 有撞机风险：挂校验回调后，全局门控未开时不生效；
 *        ON 后超限被拒且值不变、合法值放行；OFF（现场调试）门控旁路。
 * 场景 5 showArmWatchServoPower [YomkAPI::setContextMonitor(同步) / ON_MONITOR / OFF_MONITOR]
 *        安全 PLC 订阅伺服电源指令：追加式多播（上电告警 + 操作黑匣子）；
 *        全局开/关控制通知启停——OFF 后 monitor 保留但不执行。
 * 场景 6 showArmReportPose      [YomkAPI::setContextMonitor(async=true)]
 *        数字孪生异步上报末端位姿：单线程池按 set 提交序保序送达，
 *        执行线程是池线程——上报不阻塞运动控制回路。
 * 场景 7 showArmDashboard       [YomkAPI::listContextKeys / INFO_KEY / INFO_ALL]
 *        产线运维面板：键名清单、单键结构化元信息（含未命中 eNo 分支）、
 *        全量导出——框架只回结构化数据，展示格式由用户定义。
 * 场景 8 showArmTaskCleanup     [YomkAPI::destroyContext]
 *        下电流程逐键销毁；销毁后 GET 回落兜底默认值、重复销毁返回 eNo；
 *        INFO_KEYS 确认键值表已空，会话闭环。
 *
 * 共享键规划（跨场景流转，模拟多模块共享同一份状态）：
 *   arm.mode        String        运行模式（idle/teach/auto）
 *   arm.speed_ratio Float64       全局速度倍率 0.0~1.0
 *   arm.servo_power Bool          伺服电源指令状态
 *   arm.tcp_pose    Float64Array  末端位姿 [x,y,z,qx,qy,qz,qw]
 *   arm.payload_kg  Float64       末端负载 kg（全程未注册，作 GET 兜底演示）
 */

#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "YomkAPI.h"

// 定点引入所需类型，避免全局 using
using yomk::ContextChecker;

// ---------------------------------------------------------------------------
// 辅助输出：全部走 YomkLogger 控制台日志（YOMK_INFO_TAG 等），无 std::cout。
// 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
// 格式敏感的行先用 ostringstream 拼好再单参输出，避免多参数自动空格
// 把 "status=0" 拆成 "status= 0"。
// ---------------------------------------------------------------------------

// Float64 值格式化：固定 1 位小数（速度倍率语义，0.8 显示为 "0.8"、0.0 显示为 "0.0"）
static std::string fmtF64(double value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << value;
    return oss.str();
}

// 末端位姿 7 元组 [x,y,z,qx,qy,qz,qw] 格式化
static std::string fmtPose(const std::vector<double>& pose)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < pose.size(); ++i)
    {
        if (i != 0)
        {
            oss << ", ";
        }
        oss << pose[i];
    }
    oss << "]";
    return oss.str();
}

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
// 结果行标注本次接口调用；同步 checker/monitor 回调插入的日志被夹在开始行与
// 结果行之间，归属一目了然，读者顺着日志读即可理解，无需回看代码
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（GET 等返回值非 YomkResponse 的操作）：由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// GET 结果行：GET 返回 shared_ptr 而非 YomkResponse；
// isDefault 由调用方比较返回指针与兜底默认值指针得出
static void logGet(const std::string& key, const std::string& valueText, bool isDefault)
{
    std::ostringstream oss;
    oss << "[GET] " << key << " = " << valueText << (isDefault ? "（兜底默认值）" : "（命中）");
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    YOMK_INFO_TAG("arm.demo", "[验证] " + text);
}

// 自省结果展开：单键 ContextKeyInfo 的全部字段逐行展示（键名行 + 5 个字段行），
// INFO_KEY 与 INFO_ALL 共用；紧随其结果行输出，键名行以 "-" 起头、字段行缩进对齐
static void logKeyInfo(const std::string& api, const yomk::ContextKeyInfo& info)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << info.m_key;
    YOMK_INFO_TAG("arm.demo", oss.str());

    // 类型名为空表示键值为空（尚未 set），防御展示
    oss.str("");
    oss << "[" << api << "]     type: " << (info.m_typeName.empty() ? "(空)" : info.m_typeName);
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     checker: " << (info.m_checkerOn ? "on" : "off");
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     monitors: " << info.m_monitorCount;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     async: " << info.m_asyncMonitorCount;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     group: " << info.m_group;
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// CREATE 幂等化封装：键已存在（eNo "key already exists"）视为就绪跳过。
// 本身即 YomkAPI::createContext 的工程化用法：排他 eNo 在正式代码中按"已初始化"
// 处理而非错误——后启动的模块靠它安全补建自己关心的键。
static void ensureKey(const std::string& op, const std::string& key, YomkPkgPtr value)
{
    YomkResponse resp = logAction(op, "CREATE " + key, [&] { return YomkAPI::createContext(key, value); });
    if (resp.m_status != YomkResponse::eOk)
    {
        std::ostringstream oss;
        oss << "[ensureKey] " << key << " 已存在，跳过创建（" << resp.m_msg << "）";
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
}

// ---------------------------------------------------------------------------
// Checker / Monitor 回调：用户代码，框架在 set 调用链的固定时机调用。
// Checker 在写锁内调用（回调内重入 set/get 会死锁）；Monitor 在写锁外执行。
// ---------------------------------------------------------------------------

// 速度倍率安全门控：倍率是安全关键参数，超过 1.0（100%）有撞机风险，一律拒绝
ContextChecker::ECheckStatus checkerSpeedRatio(const yomk::Context& ctx)
{
    auto ratio = yomk::unPack<yomkmsg::Float64>(ctx.m_value);
    if (!ratio)
    {
        std::ostringstream oss;
        oss << "checker: " << ctx.m_key << " 值不是 Float64，无法校验 -> eReject";
        YOMK_WARN_TAG("arm.guard", oss.str());
        return ContextChecker::eReject;
    }
    if (ratio->d > 1.0)
    {
        std::ostringstream oss;
        oss << "checker: " << ctx.m_key << "=" << fmtF64(ratio->d) << " 超过安全倍率 1.0（撞机风险）-> eReject";
        YOMK_WARN_TAG("arm.guard", oss.str());
        return ContextChecker::eReject;
    }
    std::ostringstream oss;
    oss << "checker: " << ctx.m_key << "=" << fmtF64(ratio->d) << " 在安全范围内 -> eAccept";
    YOMK_INFO_TAG("arm.guard", oss.str());
    return ContextChecker::eAccept;
}

// 安全 PLC 的上电告警：伺服电源指令属安全相关事件，收到快照即告警记录
void monitorServoPowerAlarm(const yomk::Context& ctx)
{
    auto power = yomk::unPack<yomkmsg::Bool>(ctx.m_value);
    if (!power)
    {
        return;
    }
    const char* state = (power && power->d) ? "ON" : "OFF";
    std::ostringstream oss;
    oss << "[告警] 伺服电源指令: " << ctx.m_key << "=" << state << "（安全 PLC 收到本次写入快照）";
    YOMK_WARN_TAG("arm.watch", oss.str());
}

// 操作黑匣子：记录每一次伺服电源指令的快照，供事后追溯；
// 与告警回调同批执行，展示 SET_MONITOR 的追加式多播
void monitorServoPowerLog(const yomk::Context& ctx)
{
    auto power = yomk::unPack<yomkmsg::Bool>(ctx.m_value);
    if (!power)
    {
        return;
    }
    const char* state = (power && power->d) ? "ON" : "OFF";
    std::ostringstream oss;
    oss << "[黑匣子] 记录操作: " << ctx.m_key << "->" << state << "（追加式多播：与告警回调按注册序同批执行）";
    YOMK_INFO_TAG("arm.watch", oss.str());
}

// 数字孪生位姿上报：async=true 经单线程池送达（按 set 提交序保序），
// 执行线程是池线程——上报动作不阻塞运动控制回路
void monitorPoseTelemetry(const yomk::Context& ctx)
{
    auto pose = yomk::unPack<yomkmsg::Float64Array>(ctx.m_value);
    if (!pose)
    {
        return;
    }
    if (!pose)
    {
        std::ostringstream oss;
        oss << "上报失败: " << ctx.m_key << " 值不是 Float64Array";
        YOMK_WARN_TAG("arm.telemetry", oss.str());
        return;
    }
    std::ostringstream oss;
    oss << "位姿上报: " << ctx.m_key << "=" << fmtPose(pose->d) << "（池线程 " << std::this_thread::get_id()
        << "，按 set 提交序送达）";
    YOMK_INFO_TAG("arm.telemetry", oss.str());
}

// ---------------------------------------------------------------------------
// 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按作业会话顺序编排。
// 全局开关流转：checker/monitor 默认 OFF，各场景自行 arrange 并在结尾恢复
// 默认，避免跨场景干扰。
// ---------------------------------------------------------------------------

// 场景 1：控制柜上电，主控模块把机械臂初始状态写入共享上下文
static void showArmBootCreate()
{
    logScene(
        1,
        "控制柜上电初始化",
        "YomkAPI::createContext",
        "主控模块把运行模式/速度倍率/伺服状态写入共享上下文，供示教器、安全 PLC 等模块共享；"
        "重复创建同名键会被拒绝——创建是排他的。");

    logAction("写入初始运行模式", "CREATE arm.mode", [&] { return YomkAPI::createContext("arm.mode", yomkmsg::String::shared("idle")); });
    logAction(
        "写入初始速度倍率", "CREATE arm.speed_ratio", [&] { return YomkAPI::createContext("arm.speed_ratio", yomkmsg::Float64::shared(0.5)); });
    logAction(
        "写入初始伺服电源状态",
        "CREATE arm.servo_power",
        [&] { return YomkAPI::createContext("arm.servo_power", yomkmsg::Bool::shared(false)); });

    // 重复创建同名键：eNo "key already exists"，排他语义——更新请走 SET（见场景 3）
    logAction(
        "重复创建同名键",
        "CREATE arm.mode (重复)",
        [&] { return YomkAPI::createContext("arm.mode", yomkmsg::String::shared("auto")); });
    logVerify("重复创建同名键 -> eNo(key already exists)：创建排他，更新请用 SET");
}

// 场景 2：示教器 HMI 读取机械臂状态
static void showArmReadStatus()
{
    logScene(
        2,
        "示教器读取状态",
        "YomkAPI::getContext",
        "示教器启动时读取机械臂状态：命中返回只读快照；负载称重模块尚未上线，查询其键返回"
        "兜底默认值——调用方永不拿到空指针。");

    // 兜底默认值：GET 未命中时原样返回该指针（按值兜底，按指针识别兜底路径）
    yomkmsg::Float64Ptr ratioFallback = yomkmsg::Float64::shared(0.0);

    logAction("示教器读取速度倍率");
    yomkmsg::Float64Ptr ratio = YomkAPI::getContext<yomkmsg::Float64>("arm.speed_ratio", ratioFallback);
    logGet("arm.speed_ratio", fmtF64(ratio->d), ratio == ratioFallback);

    logAction("示教器查询未上线的负载模块");
    yomkmsg::Float64Ptr payload = YomkAPI::getContext<yomkmsg::Float64>("arm.payload_kg", ratioFallback);
    logGet("arm.payload_kg", fmtF64(payload->d), payload == ratioFallback);
    logVerify("查询未注册的键 -> 返回兜底默认值 0.0（指针相等），调用方永不拿到空指针");
}

// 场景 3：机械臂切自动模式并提速
static void showArmUpdateStatus()
{
    logScene(
        3,
        "运行参数调整",
        "YomkAPI::setContext",
        "切入自动模式并提速：SET 整体替换值对象，先前 GET 的持有者仍是旧快照（无撕裂读）；"
        "值类型必须与创建时一致，误写类型会被拒绝。");

    // 示教器先前读到的旧快照（0.5）——SET 之后它不应被改动
    yomkmsg::Float64Ptr ratioFallback = yomkmsg::Float64::shared(0.0);
    logAction("示教器留存当前倍率快照");
    yomkmsg::Float64Ptr oldSnapshot = YomkAPI::getContext<yomkmsg::Float64>("arm.speed_ratio", ratioFallback);
    logGet("arm.speed_ratio", fmtF64(oldSnapshot->d), oldSnapshot == ratioFallback);

    logAction("切入自动模式", "SET arm.mode", [&] { return YomkAPI::setContext("arm.mode", yomkmsg::String::shared("auto")); });
    logAction("提高速度倍率至 0.8", "SET arm.speed_ratio", [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(0.8)); });

    // 新旧快照对比：验证 SET 的整体替换语义（业务结论，不限于单次接口调用）
    logAction("新旧快照对比");
    yomkmsg::Float64Ptr fresh = YomkAPI::getContext<yomkmsg::Float64>("arm.speed_ratio", ratioFallback);
    logGet("arm.speed_ratio 旧快照", fmtF64(oldSnapshot->d), oldSnapshot == ratioFallback);
    logGet("arm.speed_ratio 新快照", fmtF64(fresh->d), fresh == ratioFallback);
    logVerify("SET 整体替换值对象：留存快照保持 0.5 不变，新读取得到 0.8——旧快照即冻结");

    // 强类型契约：arm.mode 是 String，误用 Float64 类型写入被拒
    logAction(
        "误用类型改写运行模式",
        "SET arm.mode (Float64)",
        [&] { return YomkAPI::setContext("arm.mode", yomkmsg::Float64::shared(1.0)); });
    logVerify("误用类型改写运行模式 -> eNo(context type not match)：强类型防误写");
}

// 场景 4：速度倍率安全门控（速度倍率是安全关键参数）
static void showArmGuardSpeedRatio()
{
    logScene(
        4,
        "速度倍率安全门控",
        "YomkAPI::setContextChecker / ON_CHECKER / OFF_CHECKER",
        "倍率超过 1.0（100%）有撞机风险：SET_CHECKER 挂校验回调；全局门控未开时 checker 保留但不生效；"
        "ON 后超限写入被拒且值不变、合法值放行；OFF（现场调试模式）门控旁路。");

    // 挂校验回调：仅注册。全局开关默认 OFF，此时 checker 保留在键上但不生效
    logAction(
        "挂载倍率安全校验",
        "SET_CHECKER arm.speed_ratio",
        [&] { return YomkAPI::setContextChecker("arm.speed_ratio", checkerSpeedRatio); });
    logAction(
        "门控未开时尝试超限写入",
        "SET arm.speed_ratio=1.2",
        [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(1.2)); });
    logVerify("校验已挂但全局门控未开 -> 1.2 照样写入（注意 arm.guard 日志未出现）");

    // 打开全局门控：合法值放行，超限被拒且值不变
    logAction("打开全局校验门控", "ON_CHECKER", [&] { return YomkAPI::enableContextChecker(); });
    logAction(
        "写入合法倍率 0.9",
        "SET arm.speed_ratio=0.9",
        [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(0.9)); });
    logVerify("门控打开后合法值 0.9 -> arm.guard 日志 eAccept，写入成功");
    logAction(
        "尝试写入超限倍率 1.2",
        "SET arm.speed_ratio=1.2",
        [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(1.2)); });
    logAction("确认被拒后倍率保持不变");
    yomkmsg::Float64Ptr ratioFallback = yomkmsg::Float64::shared(0.0);
    yomkmsg::Float64Ptr ratio = YomkAPI::getContext<yomkmsg::Float64>("arm.speed_ratio", ratioFallback);
    logGet("arm.speed_ratio", fmtF64(ratio->d), ratio == ratioFallback);
    logVerify("超限写入 -> eNo(checker reject set context) 且值保持 0.9 未变；arm.guard 日志给出拒绝理由");

    // 关闭全局门控（现场调试模式）：门控旁路，checker 仍在键上
    logAction("现场调试关闭门控", "OFF_CHECKER", [&] { return YomkAPI::disableContextChecker(); });
    logAction(
        "门控关闭后再次写入 1.2",
        "SET arm.speed_ratio=1.2",
        [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(1.2)); });
    logVerify("门控关闭后 1.2 -> 写入成功：门控旁路，校验保留但不生效");

    // 收尾恢复安全值，并保持全局开关为默认 OFF
    logAction(
        "恢复安全倍率 0.5",
        "SET arm.speed_ratio=0.5",
        [&] { return YomkAPI::setContext("arm.speed_ratio", yomkmsg::Float64::shared(0.5)); });
}

// 场景 5：安全 PLC 订阅伺服电源指令
static void showArmWatchServoPower()
{
    logScene(
        5,
        "伺服上下电订阅",
        "YomkAPI::setContextMonitor(同步) / ON_MONITOR / OFF_MONITOR",
        "安全 PLC 订阅伺服电源指令：SET_MONITOR 追加式注册两个同步回调（上电告警 + 操作"
        "黑匣子）；全局监控 ON 后每次写入成功即通知，OFF 后通知暂停但 monitor 保留。");

    // 追加式多播：同一键可挂多个 monitor，按注册顺序全部执行
    logAction(
        "安全 PLC 订阅上电告警",
        "SET_MONITOR arm.servo_power",
        [&] { return YomkAPI::setContextMonitor("arm.servo_power", monitorServoPowerAlarm); });
    logAction(
        "安全 PLC 订阅操作黑匣子",
        "SET_MONITOR arm.servo_power",
        [&] { return YomkAPI::setContextMonitor("arm.servo_power", monitorServoPowerLog); });

    // 对照：全局监控默认 OFF，写入静默（注意观察无 arm.watch 日志）
    logAction(
        "全局监控关闭时下发上电指令",
        "SET arm.servo_power=true",
        [&] { return YomkAPI::setContext("arm.servo_power", yomkmsg::Bool::shared(true)); });
    logVerify("全局监控默认 OFF -> 上电指令写入成功但无 arm.watch 日志（monitor 保留但不执行）");

    logAction("开启全局监控", "ON_MONITOR", [&] { return YomkAPI::enableContextMonitor(); });

    // 同步通知在 set 返回前（写锁外）执行：arm.watch 日志会夹在"开始执行"与
    // 结果行之间，属预期时序——顺着日志读即可看出通知归属这次操作
    logAction(
        "下发下电指令",
        "SET arm.servo_power=false",
        [&] { return YomkAPI::setContext("arm.servo_power", yomkmsg::Bool::shared(false)); });
    logVerify("监控开启后写入 -> 两条 arm.watch 日志夹在开始行与结果行之间（同步通知写锁外、set 返回前执行）");

    logAction("暂停全局监控", "OFF_MONITOR", [&] { return YomkAPI::disableContextMonitor(); });
    logAction(
        "再上电验证通知已暂停",
        "SET arm.servo_power=true",
        [&] { return YomkAPI::setContext("arm.servo_power", yomkmsg::Bool::shared(true)); });
    logVerify("监控暂停后写入 -> 无 arm.watch 日志：通知暂停，monitor 保留在键上");

    // 收尾保持全局监控 OFF 默认；场景 6 自行按需开启
}

// 场景 6：数字孪生异步上报末端位姿
static void showArmReportPose()
{
    logScene(
        6,
        "末端位姿遥测上报",
        "YomkAPI::setContextMonitor(async)",
        "数字孪生订阅末端位姿：async=true 的 monitor 经单线程池送达（按 set 提交序保序），"
        "执行线程是池线程——上报不阻塞运动控制回路。");

    // 位姿键可能晚于主控创建（数字孪生模块后启动），幂等补建
    ensureKey("数字孪生创建位姿", "arm.tcp_pose", yomkmsg::Float64Array::shared(std::vector<double>{0, 0, 0, 0, 0, 0, 1}));

    // arrange：全局监控在场景 5 收尾已恢复 OFF，此处按需开启
    logAction("开启全局监控", "ON_MONITOR", [&] { return YomkAPI::enableContextMonitor(); });
    logAction(
        "数字孪生订阅异步位姿上报",
        "SET_MONITOR arm.tcp_pose (async)",
        [&] { return YomkAPI::setContextMonitor("arm.tcp_pose", monitorPoseTelemetry, true); });

    std::ostringstream ossThreadId;
    ossThreadId << "主线程 id: " << std::this_thread::get_id();
    YOMK_INFO_TAG("arm.demo", ossThreadId.str());

    // 连续 3 次位姿更新（模拟插补运动）：提交序 #1 -> #2 -> #3
    logAction(
        "插补运动 #1",
        "SET arm.tcp_pose",
        [&] { return YomkAPI::setContext("arm.tcp_pose", yomkmsg::Float64Array::shared(std::vector<double>{100, 0, 200, 0, 0, 0, 1})); });
    logAction(
        "插补运动 #2",
        "SET arm.tcp_pose",
        [&] {
            return YomkAPI::setContext("arm.tcp_pose", yomkmsg::Float64Array::shared(std::vector<double>{120, 40, 220, 0, 0, 0.707, 0.707}));
        });
    logAction(
        "插补运动 #3",
        "SET arm.tcp_pose",
        [&] { return YomkAPI::setContext("arm.tcp_pose", yomkmsg::Float64Array::shared(std::vector<double>{150, 80, 250, 0, 0, 1, 0})); });

    YOMK_INFO_TAG("arm.demo", "等待异步上报送达（单线程池按提交序执行）...");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    logVerify("3 条 arm.telemetry 日志按插补序 #1->#2->#3 到达且线程为池线程（与主线程 id 不同）——不阻塞控制回路");

    // 收尾恢复全局监控 OFF 默认
    logAction("停止位姿上报", "OFF_MONITOR", [&] { return YomkAPI::disableContextMonitor(); });
}

// 场景 7：产线运维面板巡检机械臂共享状态
static void showArmDashboard()
{
    logScene(
        7,
        "产线运维面板",
        "YomkAPI::listContextKeys / INFO_KEY / INFO_ALL",
        "运维面板巡检：INFO_KEYS 拉取键名清单；INFO_KEY 抽查单键注册详情（结构化 "
        "ContextKeyInfo）；INFO_ALL 全量导出——框架只回结构化数据，展示格式由用户定义。");

    // 键名清单（StringArray 结构化包）
    YomkResponse keysResp = logAction("巡检键名清单", "INFO_KEYS", [&] { return YomkAPI::listContextKeys(); });
    auto keyArr = yomk::unPack<yomkmsg::StringArray>(keysResp.m_data);
    if (keyArr)
    {
        for (const auto& key : keyArr->d)
        {
            YOMK_INFO_TAG("arm.demo", std::string("[INFO_KEYS] - ") + key);
        }
    }

    // 单键抽查：速度倍率——场景 4 挂的门控回调仍在键上
    YomkResponse ratioResp = logAction(
        "抽查速度倍率注册详情", "INFO_KEY arm.speed_ratio", [&] { return YomkAPI::getContextInfo("arm.speed_ratio"); });
    auto ratioInfo = yomk::unPack<yomkmsg::ContextKeyInfo>(ratioResp.m_data);
    if (ratioInfo)
    {
        logKeyInfo("INFO_KEY", ratioInfo->d);
    }
    logVerify("arm.speed_ratio：checker=on（场景 4 的门控回调仍在）");

    // 单键抽查：末端位姿——异步上报 monitor 已追加
    YomkResponse poseResp = logAction(
        "抽查末端位姿注册详情", "INFO_KEY arm.tcp_pose", [&] { return YomkAPI::getContextInfo("arm.tcp_pose"); });
    auto poseInfo = yomk::unPack<yomkmsg::ContextKeyInfo>(poseResp.m_data);
    if (poseInfo)
    {
        logKeyInfo("INFO_KEY", poseInfo->d);
    }
    logVerify("arm.tcp_pose：monitors=1、async=1（场景 6 的异步上报仍在，async 是 monitors 中异步的数量）");

    // 未命中惯例：查询未注册键返回 eNo
    logAction("查询未注册键", "INFO_KEY arm.ghost", [&] { return YomkAPI::getContextInfo("arm.ghost"); });
    logVerify("查询未注册的键 -> eNo(key is not exist)");

    // 全量导出（组 → 键元信息两层结构化包），逐组逐键展开全部字段
    YomkResponse allResp = logAction("全量导出键值表", "INFO_ALL", [&] { return YomkAPI::getAllContextInfo(); });
    auto allArr = yomk::unPack<yomkmsg::ContextGroupInfoList>(allResp.m_data);
    if (allArr)
    {
        for (const auto& group : allArr->d)
        {
            for (const auto& info : group.m_keys)
            {
                logKeyInfo("INFO_ALL", info);
            }
        }
    }
}

// 场景 8：机械臂下电流程，逐键清理共享状态
static void showArmTaskCleanup()
{
    logScene(
        8,
        "下电清理",
        "YomkAPI::destroyContext",
        "下电流程逐键销毁共享状态；销毁后一切操作按 not-found 惯例返回 eNo，"
        "GET 回落兜底默认值。");

    logAction("销毁末端位姿键", "DESTROY arm.tcp_pose", [&] { return YomkAPI::destroyContext("arm.tcp_pose"); });
    logAction("销毁伺服电源键", "DESTROY arm.servo_power", [&] { return YomkAPI::destroyContext("arm.servo_power"); });
    logAction("销毁速度倍率键", "DESTROY arm.speed_ratio", [&] { return YomkAPI::destroyContext("arm.speed_ratio"); });
    logAction("销毁运行模式键", "DESTROY arm.mode", [&] { return YomkAPI::destroyContext("arm.mode"); });

    // 销毁后行为：GET 回落兜底默认值
    yomkmsg::StringPtr modeFallback = yomkmsg::String::shared("unknown");
    logAction("确认销毁后读取回落");
    yomkmsg::StringPtr mode = YomkAPI::getContext<yomkmsg::String>("arm.mode", modeFallback);
    logGet("arm.mode", mode ? mode->d : "(null)", mode == modeFallback);
    logVerify("销毁后 GET -> 返回兜底默认值（指针相等），键已消失");

    // 重复销毁：not-found 惯例
    logAction(
        "重复销毁运行模式键",
        "DESTROY arm.mode (重复)",
        [&] { return YomkAPI::destroyContext("arm.mode"); });
    logVerify("重复销毁同名键 -> eNo(key is not exist)");

    // 收尾确认：键值表已空，会话闭环
    YomkResponse keysResp = logAction("收尾确认键值表已空", "INFO_KEYS", [&] { return YomkAPI::listContextKeys(); });
    auto keyArr = yomk::unPack<yomkmsg::StringArray>(keysResp.m_data);
    logVerify(keyArr && keyArr->d.empty() ? "键值表已空：机械臂会话结束，共享状态全部下线" : "键值表非空：存在未清理键");
}

int main(int argc, char* argv[])
{
    // 初始化框架（上下文服务 /YomkContext 随之自动启动）
    YomkAPI::init();

    YOMK_INFO_TAG("arm.demo", "机械臂控制 x YomkContext 示例：一次作业会话贯穿 8 个场景，覆盖全部上下文 API");
    YOMK_INFO_TAG("arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.guard=校验 / arm.watch=通知 / arm.telemetry=上报");

    showArmBootCreate();
    showArmReadStatus();
    showArmUpdateStatus();
    showArmGuardSpeedRatio();
    showArmWatchServoPower();
    showArmReportPose();
    showArmDashboard();
    showArmTaskCleanup();

    YOMK_INFO_TAG("arm.demo", "========== 示例结束：按回车退出 ==========");
    getchar();

    return 0;
}
