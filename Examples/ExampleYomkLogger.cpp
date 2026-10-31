/**
 * @file ExampleYomkLogger.cpp
 * @brief YomkLogger 日志系统示例：机械臂产线背景的 6 个场景小示例（面向初学者）
 *
 * 日志系统提供开箱即用的双路输出：控制台日志（四级级别 + tag 分层 + 代理拦截）
 * 与文件日志（内存缓冲 + 显式刷盘 + 级别门控 + 代理拦截），并附带日志器内省。
 * 随 YomkAPI::init() 自动启动。
 * 机械臂产线中运行足迹走控制台（班前点检、班中预警、班后清档），关键事件另存
 * 黑匣子与班次存档（文件日志），运维班前班后用内看点检日志系统本身。
 *
 * 控制台输出格式四段：[时间] [级别] [tag] [行号] 内容——tag 即控制台日志器名，
 * 首次使用自动创建；宏自动把 [行号] 拼进内容开头，方便定位调用处。
 * 级别开关两级并联：无参作用于全局主闸（框架全局）、具名逐器门控单个日志器，
 * 输出/落盘取两层 AND；控制台开关与文件开关两套相互独立，文件开关只影响写入。
 *
 * 本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
 * 全部 40 个 YOMK 日志用户宏（控制台 8 / 开关 16 / 代理 2 / 文件 10 / 内省 4）
 * 无一遗漏。叙事日志统一走 arm.demo（YomkLogger 自身即演示对象）。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmLogLevels   [YOMK_INFO/WARN/ERROR/DEBUG + _TAG 版]
 *        班前点检报文分级：四级日志按 DEBUG 详单 -> INFO 通报 -> WARN 预警 ->
 *        ERROR 告警递进；默认 tag 与自定义 tag 双填兼容，[行号] 随行定位。
 * 场景 2 showArmSilentMode  [disable/enableConsoleLog*（无参/具名）+ LOGGER_INFO_GLOBAL + LOGGER_INFO_LOGGER]
 *        产线静默模式：全局静默四级别逐级全关（空窗由 WARN/ERROR 锚点桥接，调用仍返回
 *        eOk）；逐级恢复后单查 MainLogger 状态段复查五开关全 on；随后逐器静默——具名
 *        关断 arm.joint 仅其报文消失（MainLogger/arm.demo 对照不受影响），
 *        LOGGER_INFO_GLOBAL 报主闸全 on 与条目 infoOff 两层归因唯一。
 * 场景 3 showArmBlackbox    [setConsoleLogProxy / setFileLogProxy / FILE_LOG_CREATE / WRITE]
 *        安全黑匣子：代理把 WARN/ERROR 转写文件后消费、DEBUG 噪声消化、INFO
 *        放行——结果行 msg 区分消费与放行；传 nullptr 卸载恢复默认。文件代理段
 *        用 GLOBAL("file") 复查 proxy 态并演示拦截/卸载，与控制台代理相互独立。
 * 场景 4 showArmShiftLog    [YOMK_FILE_INFO/WARN/ERROR/DEBUG + _TAG 版 + disable/enableFileLog* + GLOBAL("file")]
 *        班次存档：双 tag 四级写入内存缓冲，显式 WRITE 刷盘；控制台 DEBUG 全关
 *        期间文件 DEBUG 照常落盘（两套开关独立）；文件具名关断期间 INFO 静默
 *        吞条而文件主闸仍 on——两层归因，恢复后落盘回归。
 * 场景 5 showArmLoggerDashboard [LOGGER_INFO_LOGGERS / LOGGER / ALL + LOGGER_INFO_GLOBAL("console"/"file")]
 *        运维面板：console/file 清单（字典序）、单查结构化 LoggerInfo（含未
 *        注册 eNo）、全量导出——console/file 条目各自承载逐器门控层真值与
 *        对应侧代理态，再查 LOGGER_INFO_GLOBAL 按 type 复核两套主闸态，
 *        两层一起看全。
 * 场景 6 showArmLogCleanup  [FILE_LOG_DELETE + 惰性重建]
 *        收班清档：DELETE 同名清 console+file 两表（返回清了几张表）、磁盘
 *        .log 保留；MainLogger 删除后再写自动重建（按名惰性创建）。
 *
 * tag 规划（arm.demo 叙事之外，演示对象按业务分层）：
 *   MainLogger  默认 tag（无 TAG 宏的日志归属）        框架预创建
 *   arm.joint   关节模块报文（场景 1/4 演示自定义 tag） 首次使用自动创建
 *   blackbox    安全黑匣子文件器（场景 3 创建/6 清档）  WARN/ERROR 转写落盘
 *   shiftlog    班次存档文件器（场景 4 创建/6 清档）    全级别存档落盘
 */

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

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
// 日志宏的结果行自带流向证据：代理消费 msg="console log proxy is success."、放行 msg="success."。
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
// 结果行标注本次接口调用；被演示的日志行（控制台）夹在开始行与结果行之间，
// 归属一目了然，读者顺着日志读即可理解，无需回看代码
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如 LOGGER_INFO_* / 文件回读）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// 自省结果展开：单个日志器 LoggerInfo 的字段逐行展示（名称行 + type/状态行），
// LOGGER_INFO_LOGGER 与 LOGGER_INFO_ALL 共用；console 条目状态段单行承载五开关
static void logLoggerInfo(const std::string& api, const yomk::LoggerInfo& info)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << info.m_loggerName;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     type: " << info.m_type;
    YOMK_INFO_TAG("arm.demo", oss.str());

    if (info.m_type == "file")
    {
        oss.str("");
        oss << "[" << api << "]     dir: " << info.m_dir;
        YOMK_INFO_TAG("arm.demo", oss.str());
        return;
    }
    const yomk::LoggerState& state = info.m_loggerState;
    oss.str("");
    oss << "[" << api << "]     state: debug=" << (state.m_debugOn ? "on" : "off")
        << " info=" << (state.m_infoOn ? "on" : "off") << " warn=" << (state.m_warnOn ? "on" : "off")
        << " error=" << (state.m_errorOn ? "on" : "off") << " proxy=" << (state.m_proxyOn ? "on" : "off");
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// LOGGER_INFO_LOGGERS 清单行：日志器名列表逐行展示（" - " 无序列表风格）
static void logLoggerLine(const std::string& api, const std::string& loggerName)
{
    YOMK_INFO_TAG("arm.demo", "[" + api + "] - " + loggerName);
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    YOMK_INFO_TAG("arm.demo", "[验证] " + text);
}

// 回读 .log 文件逐行输出（arm.demo tag）——眼见为实的落盘验证，零 std::cout
static void logFileContent(const fs::path& path)
{
    std::ifstream logFile(path);
    std::string line;
    while (std::getline(logFile, line))
    {
        YOMK_INFO_TAG("arm.demo", "| " + line);
    }
}

// ---------------------------------------------------------------------------
// 控制台日志代理（场景 3）：关键事件转写黑匣子文件后消费、DEBUG 噪声消化、INFO 放行。
// 框架每写一条控制台日志先过本回调：返回 false 日志被消费（不再默认输出），
// 返回 true 放行继续框架默认打印。回调在调用方线程同步执行，内部只走文件日志
// （文件路径不经过代理，无递归），不可再打控制台日志。
// ---------------------------------------------------------------------------
bool blackboxProxy(const yomk::Log& log)
{
    // WARN/ERROR 关键事件：预拼一行（原文 + tag）转写黑匣子，控制台不再输出
    if (log.m_level == yomk::Log::eWarn || log.m_level == yomk::Log::eError)
    {
        std::ostringstream oss;
        oss << log.m_log << " (tag: " << log.m_tag << ")";
        if (log.m_level == yomk::Log::eWarn)
        {
            YOMK_FILE_WARN("blackbox", oss.str());
        }
        else
        {
            YOMK_FILE_ERROR("blackbox", oss.str());
        }
        return false;
    }
    // DEBUG 噪声：直接消费丢弃
    if (log.m_level == yomk::Log::eDebug)
    {
        return false;
    }
    // 其余（INFO）：放行，继续框架默认输出
    return true;
}

// ---------------------------------------------------------------------------
// 文件日志代理（场景 3）：拦截 DEBUG 噪声不落盘、其余放行走默认落盘。
// 框架每写一条文件日志先过本回调（先于文件器查找与级别门控）：返回 false 该条
// 不写入内存缓冲，返回 true 穿透继续默认落盘。回调在调用方线程同步执行，
// 内部不要再写文件日志（会重入本代理），语义与控制台代理对称、相互独立。
// ---------------------------------------------------------------------------
bool fileDebugProxy(const yomk::Log& log)
{
    return log.m_level != yomk::Log::eDebug;  // 仅拦截 DEBUG，其余放行
}

// ---------------------------------------------------------------------------
// 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
// 文件器流转：blackbox/shiftlog 场景 3/4 创建，场景 6 一并清档。
// ---------------------------------------------------------------------------

// 场景 1：班前点检报文分级（四级 + tag 分层）
static void showArmLogLevels()
{
    logScene(
        1,
        "班前点检报文分级",
        "YOMK_INFO/WARN/ERROR/DEBUG + _TAG 版",
        "班前点检报文按紧迫度分级：DEBUG 轨迹详单、INFO 流程通报、WARN 隐患预警、"
        "ERROR 故障告警——输出格式 [时间] [级别] [tag] [行号] 内容，[行号] 由宏自动拼接定位调用处。");

    // 默认 tag：MainLogger（框架预创建，无 TAG 宏的日志均归属它）
    logAction("记录轨迹详单（DEBUG）", "YOMK_DEBUG", [&] { return YOMK_DEBUG("插补周期 4ms，指令缓冲占用 12%"); });
    logAction("记录流程通报（INFO）", "YOMK_INFO", [&] { return YOMK_INFO("班前点检通过：产线就绪，工单 W-1024 下发"); });
    logAction("上报隐患预警（WARN）", "YOMK_WARN", [&] { return YOMK_WARN("减速器油温 68℃，接近 70℃ 预警线"); });
    logAction("上报故障告警（ERROR）", "YOMK_ERROR", [&] { return YOMK_ERROR("j2 轴伺服过流，已触发保护区急停"); });
    logVerify("四条日志级别字段各异、[行号] 即调用处：被演示的日志行夹在开始行与结果行之间，归属一目了然");

    // 自定义 tag：arm.joint（tag 兼容双填兼作日志器名，首次使用自动创建）
    logAction("关节模块记录到位通报（INFO_TAG）", "YOMK_INFO_TAG arm.joint",
        [&] { return YOMK_INFO_TAG("arm.joint", "j1 轴到位，定位偏差 0.02mm"); });
    logAction("关节模块记录电流详单（DEBUG_TAG）", "YOMK_DEBUG_TAG arm.joint",
        [&] { return YOMK_DEBUG_TAG("arm.joint", "j1 轴电流 3.2A，力矩 12Nm"); });
    logAction("关节模块上报温升预警（WARN_TAG）", "YOMK_WARN_TAG arm.joint",
        [&] { return YOMK_WARN_TAG("arm.joint", "j2 轴温升 15℃/min，建议降速"); });
    logAction("关节模块上报通信故障（ERROR_TAG）", "YOMK_ERROR_TAG arm.joint",
        [&] { return YOMK_ERROR_TAG("arm.joint", "j3 轴编码器通信超时 200ms"); });
    logVerify("tag 随记录显示且兼作日志器名（首次使用自动创建）：arm.joint 报文与叙事 arm.demo 按分层阅读");
}

// 场景 2：产线静默模式（四级别逐级全关 -> 逐级恢复）
// 注：级别关闭期间 INFO 叙事同样被抑制——空窗由 WARN/ERROR 锚点桥接，这本身
// 就是"开关只抑制输出、调用始终成功"的现场演示
static void showArmSilentMode()
{
    logScene(
        2,
        "产线静默模式",
        "disable/enableConsoleLog*（无参/具名） + LOGGER_INFO_GLOBAL + LOGGER_INFO_LOGGER",
        "夜间无人值守降噪：四级别逐级全关（调用仍返回 eOk，仅输出被抑制），"
        "空窗段无任何控制台输出；恢复后单查状态段复查五开关全 on。");

    // 先关 DEBUG 详单（最常用的降噪手段）
    logAction("关闭 DEBUG 详单（产线静默）", "disableConsoleLogDebug", [&] { return YomkAPI::disableConsoleLogDebug(); });
    logVerify("随后的 DEBUG 调用（静默段）仍返回 eOk 但不再输出：级别开关只抑制输出、不影响调用成功");

    YOMK_WARN_TAG("arm.demo", "继续降噪：INFO/WARN/ERROR 也将逐级关闭，空窗期间操作照常执行");
    YOMK_ERROR_TAG("arm.demo", "最后一条可见日志：ERROR 关闭后全静默，恢复后汇报空窗详情");

    logAction("关闭 INFO 流程通报", "disableConsoleLogInfo", [&] { return YomkAPI::disableConsoleLogInfo(); });
    logAction("关闭 WARN 隐患预警", "disableConsoleLogWarn", [&] { return YomkAPI::disableConsoleLogWarn(); });
    logAction("关闭 ERROR 故障告警", "disableConsoleLogError", [&] { return YomkAPI::disableConsoleLogError(); });

    // 全静默段：以下 4 条调用成功返回 eOk，控制台一行都不出现
    YOMK_INFO("你看不到我：INFO 已关闭");
    YOMK_WARN("你看不到我：WARN 已关闭");
    YOMK_ERROR("你看不到我：ERROR 已关闭");
    YOMK_DEBUG("你看不到我：DEBUG 已关闭");

    // 恢复：ERROR/INFO 先裸调恢复（INFO 回归后叙事锚点可见），随后逐级恢复
    YomkAPI::enableConsoleLogError();
    YomkAPI::enableConsoleLogInfo();
    YOMK_INFO_TAG("arm.demo",
        "静默解除：空窗期间已按序执行 OFF_INFO/WARN/ERROR 与 4 条演示调用，全部返回 eOk——"
        "恢复 WARN/DEBUG 后复查");
    logAction("恢复 WARN 隐患预警", "enableConsoleLogWarn", [&] { return YomkAPI::enableConsoleLogWarn(); });
    logAction("恢复 DEBUG 详单", "enableConsoleLogDebug", [&] { return YomkAPI::enableConsoleLogDebug(); });

    logAction("复查 MainLogger 开关状态");
    YomkResponse resp = YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console");
    logResp("LOGGER_INFO_LOGGER MainLogger(console)", resp);
    auto info = yomk::unPack<yomkmsg::LoggerInfo>(resp.m_data);
    if (info)
    {
        logLoggerInfo("LOGGER_INFO_LOGGER", info->d);
    }
    logVerify("五开关全 on：无参 OFF/ON 作用于全局主闸，状态段报逐器门控层真值（本段未用具名开关，两者一致）");

    // 逐器静默：具名开关仅门控单个日志器，全局主闸不受影响（两层归因现场演示）
    logAction("逐器静默：仅关闭 arm.joint 的 INFO（具名关断）", "disableConsoleLogInfo arm.joint",
        [&] { return YomkAPI::disableConsoleLogInfo("arm.joint"); });
    logAction("关节模块上报工位动态（INFO，arm.joint 已被具名关断）", "YOMK_INFO_TAG arm.joint",
        [&] { return YOMK_INFO_TAG("arm.joint", "工位 A 首件下线（本条仅 arm.joint 静默，你看不到）"); });
    YOMK_INFO_TAG("arm.demo", "arm.joint 已逐器静默：arm.demo 叙事与 MainLogger 输出不受影响（对照锚点）");

    logAction("复查全局主闸状态");
    YomkResponse gResp = YomkAPI::LOGGER_INFO_GLOBAL();
    logResp("LOGGER_INFO_GLOBAL", gResp);
    auto gs = yomk::unPack<yomkmsg::LoggerState>(gResp.m_data);
    if (gs)
    {
        std::ostringstream oss;
        oss << "[LOGGER_INFO_GLOBAL] 主闸: debug=" << (gs->d.m_debugOn ? "on" : "off")
            << " info=" << (gs->d.m_infoOn ? "on" : "off") << " warn=" << (gs->d.m_warnOn ? "on" : "off")
            << " error=" << (gs->d.m_errorOn ? "on" : "off") << " proxy=" << (gs->d.m_proxyOn ? "on" : "off");
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
    YomkResponse jResp = YomkAPI::LOGGER_INFO_LOGGER("arm.joint", "console");
    auto jInfo = yomk::unPack<yomkmsg::LoggerInfo>(jResp.m_data);
    if (jInfo)
    {
        logLoggerInfo("LOGGER_INFO_LOGGER arm.joint", jInfo->d);
    }
    logVerify("主闸 info=on 而条目 info=off——两层状态各自真实呈现：输出被关是逐器层所致，具名 ON 即恢复");
    logAction("逐器恢复：仅重开 arm.joint 的 INFO（具名恢复）", "enableConsoleLogInfo arm.joint",
        [&] { return YomkAPI::enableConsoleLogInfo("arm.joint"); });
    logAction("关节模块恢复上报（INFO，输出回归）", "YOMK_INFO_TAG arm.joint",
        [&] { return YOMK_INFO_TAG("arm.joint", "arm.joint INFO 已恢复，报文重新可见"); });
}

// 场景 3：安全黑匣子（控制台日志代理：消费与放行）
static void showArmBlackbox(const std::string& logDir)
{
    logScene(
        3,
        "安全黑匣子",
        "setConsoleLogProxy / setFileLogProxy / FILE_LOG_CREATE / WRITE",
        "关键事件不能只留控制台：安装代理后每条控制台日志先过回调——WARN/ERROR "
        "转写黑匣子文件后消费、DEBUG 噪声就地消化、INFO 放行，结果行 msg 区分"
        "消费与放行；文件代理同款语义作用于文件写入，两套代理相互独立。");

    logAction(
        "创建安全黑匣子文件器 blackbox", "FILE_LOG_CREATE blackbox", [&] { return YomkAPI::FILE_LOG_CREATE(logDir, "blackbox"); });
    logAction("安装控制台日志代理", "setConsoleLogProxy blackboxProxy", [&] { return YomkAPI::setConsoleLogProxy(blackboxProxy); });

    logAction("上报班中通报（INFO，代理放行）", "YOMK_INFO", [&] { return YOMK_INFO("工位 A 完成工单 W-1024 首件下线"); });
    logAction("记录调试详单（DEBUG，代理消费）", "YOMK_DEBUG", [&] { return YOMK_DEBUG("视觉对位耗时 320ms，阈值 500ms"); });
    logAction("上报隐患预警（WARN，代理转写黑匣子）", "YOMK_WARN", [&] { return YOMK_WARN("工位 B 气压 0.52MPa，低于额定 0.6MPa"); });
    logAction("上报故障告警（ERROR，代理转写黑匣子）", "YOMK_ERROR", [&] { return YOMK_ERROR("传送带电机堵转，工位 C 停机"); });
    logVerify(
        "结果行 msg 自证去向：放行 msg=success.、消费 msg=console log proxy is success.——"
        "4 条演示中仅 INFO 出现在控制台，WARN/ERROR 已转写黑匣子");

    logAction("查看代理安装状态");
    YomkResponse resp = YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console");
    logResp("LOGGER_INFO_LOGGER MainLogger(console)", resp);
    auto proxyInfo = yomk::unPack<yomkmsg::LoggerInfo>(resp.m_data);
    if (proxyInfo)
    {
        logLoggerInfo("LOGGER_INFO_LOGGER", proxyInfo->d);
    }
    logVerify("state 段 proxy=on：代理是框架全局单点，接管全部控制台日志");

    logAction("刷盘安全黑匣子", "FILE_LOG_WRITE blackbox", [&] { return YomkAPI::FILE_LOG_WRITE("blackbox"); });
    logAction("回读黑匣子文件验证转写");
    YOMK_INFO_TAG("arm.demo", "---- blackbox.log 内容 ----");
    logFileContent(fs::path(logDir) / "blackbox.log");
    logVerify("blackbox.log 仅 2 条关键事件（WARN/ERROR）：黑匣子只留关键事件，噪声就地消化");

    logAction("卸载代理恢复默认输出", "setConsoleLogProxy nullptr", [&] { return YomkAPI::setConsoleLogProxy(nullptr); });
    logAction("复查代理状态");
    resp = YomkAPI::LOGGER_INFO_LOGGER("MainLogger", "console");
    logResp("LOGGER_INFO_LOGGER MainLogger(console)", resp);
    auto offInfo = yomk::unPack<yomkmsg::LoggerInfo>(resp.m_data);
    if (offInfo)
    {
        logLoggerInfo("LOGGER_INFO_LOGGER", offInfo->d);
    }
    logVerify("proxy 回到 off：传 nullptr 即卸载，控制台恢复框架默认输出格式");

    // 文件代理段：同一套拦截语义作用在文件写入路径，GLOBAL("file") 单查文件侧状态
    logAction("安装文件日志代理", "setFileLogProxy fileDebugProxy", [&] { return YomkAPI::setFileLogProxy(fileDebugProxy); });
    resp = YomkAPI::LOGGER_INFO_GLOBAL("file");
    logResp("LOGGER_INFO_GLOBAL file", resp);
    auto fgs = yomk::unPack<yomkmsg::LoggerState>(resp.m_data);
    if (fgs)
    {
        std::ostringstream oss;
        oss << "[LOGGER_INFO_GLOBAL file] 文件主闸: debug=" << (fgs->d.m_debugOn ? "on" : "off")
            << " info=" << (fgs->d.m_infoOn ? "on" : "off") << " warn=" << (fgs->d.m_warnOn ? "on" : "off")
            << " error=" << (fgs->d.m_errorOn ? "on" : "off") << " proxy=" << (fgs->d.m_proxyOn ? "on" : "off");
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
    logAction("记录调试详单（DEBUG，文件代理拦截）", "YOMK_FILE_DEBUG blackbox", [&] { return YOMK_FILE_DEBUG("blackbox", "视觉对位耗时 350ms，阈值 500ms"); });
    logAction("卸载文件代理恢复落盘", "setFileLogProxy nullptr", [&] { return YomkAPI::setFileLogProxy(nullptr); });
    logAction("卸载后记录调试详单（DEBUG，穿透落盘）", "YOMK_FILE_DEBUG blackbox", [&] { return YOMK_FILE_DEBUG("blackbox", "卸载代理后此行正常写入缓冲"); });
    logVerify("proxy=on 时 DEBUG 被拦（msg=file log proxy is success.）、nullptr 卸载后 DEBUG 恢复写入——文件代理与控制台代理相互独立");
}

// 场景 4：班次存档（文件日志：缓冲、刷盘、与控制台独立）
static void showArmShiftLog(const std::string& logDir)
{
    logScene(
        4,
        "班次存档",
        "YOMK_FILE_INFO/WARN/ERROR/DEBUG + _TAG 版 + disable/enableFileLog* + GLOBAL(\"file\")",
        "全级别班次存档：FILE 系列宏写入内存缓冲，WRITE 显式刷盘到 dir/name.log；"
        "文件开关与控制台开关两套相互独立——控制台 DEBUG 全关期间文件 DEBUG 照常"
        "落盘，文件具名关断期间条目静默吞条（msg 仍 success.），落盘与否回读实证。");

    logAction(
        "创建班次存档文件器 shiftlog", "FILE_LOG_CREATE shiftlog", [&] { return YomkAPI::FILE_LOG_CREATE(logDir, "shiftlog"); });

    // 默认 tag 写入（内容前缀仅 [行号]）
    logAction("存档流程通报（INFO）", "YOMK_FILE_INFO shiftlog", [&] { return YOMK_FILE_INFO("shiftlog", "班次开始，工单 W-1024 就绪"); });
    logAction("存档隐患预警（WARN）", "YOMK_FILE_WARN shiftlog", [&] { return YOMK_FILE_WARN("shiftlog", "工位 A 气压 0.55MPa，低于额定 0.6"); });
    logAction("存档故障告警（ERROR）", "YOMK_FILE_ERROR shiftlog", [&] { return YOMK_FILE_ERROR("shiftlog", "工位 B 视觉检测连续 3 次超时"); });
    logAction("存档节拍详单（DEBUG）", "YOMK_FILE_DEBUG shiftlog", [&] { return YOMK_FILE_DEBUG("shiftlog", "节拍 12.4s/件，缓存占用 31%"); });

    // 自定义 tag 写入（内容前缀 [tag] [行号]，文件内区分模块）
    logAction("关节模块存档插补通报（INFO_TAG）", "YOMK_FILE_INFO_TAG shiftlog arm.joint",
        [&] { return YOMK_FILE_INFO_TAG("shiftlog", "arm.joint", "j1 轴完成 1200 次插补"); });
    logAction("关节模块存档温升预警（WARN_TAG）", "YOMK_FILE_WARN_TAG shiftlog arm.joint",
        [&] { return YOMK_FILE_WARN_TAG("shiftlog", "arm.joint", "j2 轴温度 71℃，建议班后保养"); });
    logAction("关节模块存档力矩超限告警（ERROR_TAG）", "YOMK_FILE_ERROR_TAG shiftlog arm.joint",
        [&] { return YOMK_FILE_ERROR_TAG("shiftlog", "arm.joint", "j3 轴力矩超限 118%"); });
    logAction("关节模块存档间隙补偿详单（DEBUG_TAG）", "YOMK_FILE_DEBUG_TAG shiftlog arm.joint",
        [&] { return YOMK_FILE_DEBUG_TAG("shiftlog", "arm.joint", "j4 轴反向间隙补偿 0.03mm"); });

    // 交叉验证：控制台 DEBUG 全关期间，文件 DEBUG 照常落盘
    logAction("关闭 DEBUG 详单（交叉验证）", "disableConsoleLogDebug", [&] { return YomkAPI::disableConsoleLogDebug(); });
    logAction("开关关闭期间存档详单（DEBUG）", "YOMK_FILE_DEBUG shiftlog",
        [&] { return YOMK_FILE_DEBUG("shiftlog", "交叉验证：控制台 DEBUG 已关，此行仍应落盘"); });
    logAction("恢复 DEBUG 详单", "enableConsoleLogDebug", [&] { return YomkAPI::enableConsoleLogDebug(); });

    // 存档静默段：文件具名关断期间 INFO 静默吞条（msg 仍 success.），文件主闸仍 on——两层归因
    logAction("存档静默：具名关断 shiftlog 的 INFO", "disableFileLogInfo shiftlog", [&] { return YomkAPI::disableFileLogInfo("shiftlog"); });
    logAction("静默期间存档通报（INFO，被门控吞掉）", "YOMK_FILE_INFO shiftlog", [&] { return YOMK_FILE_INFO("shiftlog", "静默期间此行不应落盘"); });
    YomkResponse resp = YomkAPI::LOGGER_INFO_GLOBAL("file");
    logResp("LOGGER_INFO_GLOBAL file", resp);
    auto fgs = yomk::unPack<yomkmsg::LoggerState>(resp.m_data);
    if (fgs)
    {
        std::ostringstream oss;
        oss << "[LOGGER_INFO_GLOBAL file] 文件主闸 info=" << (fgs->d.m_infoOn ? "on" : "off")
            << " proxy=" << (fgs->d.m_proxyOn ? "on" : "off") << "（主闸仍 on：缺席归因逐器门控层）";
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
    logVerify("文件主闸仍全 on：INFO 缺席归因逐器门控层，两层状态各自真实呈现");
    logAction("恢复 shiftlog 的 INFO", "enableFileLogInfo shiftlog", [&] { return YomkAPI::enableFileLogInfo("shiftlog"); });

    logAction("显式刷盘班次存档", "FILE_LOG_WRITE shiftlog", [&] { return YomkAPI::FILE_LOG_WRITE("shiftlog"); });
    logAction("回读班次存档文件");
    YOMK_INFO_TAG("arm.demo", "---- shiftlog.log 内容 ----");
    logFileContent(fs::path(logDir) / "shiftlog.log");
    logVerify("9 条四级双 tag 落盘、静默段 INFO 缺席：文件开关只影响文件写入，与控制台开关相互独立（写缓冲在先、显式 WRITE 落盘在后）");
}

// 场景 5：日志系统运维面板（内省三件套 + 全局主闸态单查）
static void showArmLoggerDashboard()
{
    logScene(
        5,
        "日志系统运维面板",
        "LOGGER_INFO_LOGGERS / LOGGER / ALL + LOGGER_INFO_GLOBAL(\"console\"/\"file\")",
        "班中巡检日志系统：LOGGERS 按类型拉取日志器名清单（字典序）；LOGGER 按类型"
        "单查结构化 LoggerInfo（两表允许同名，须精确指定）；ALL 全量导出——"
        "框架只回结构化数据，展示格式由用户定义；末尾 LOGGER_INFO_GLOBAL 按 type"
        "分流单查控制台/文件两套主闸态，与条目逐器真值两层一起看全。");

    logAction("巡检控制台日志器清单");
    YomkResponse consoleResp = YomkAPI::LOGGER_INFO_LOGGERS("console");
    logResp("LOGGER_INFO_LOGGERS console", consoleResp);
    auto consoleArr = yomk::unPack<yomkmsg::StringArray>(consoleResp.m_data);
    if (consoleArr)
    {
        for (const auto& loggerName : consoleArr->d)
        {
            logLoggerLine("LOGGER_INFO_LOGGERS", loggerName);
        }
    }
    logVerify("MainLogger（预创建）与 arm.demo/arm.joint（tag 惰性创建）均在列：清单按名字字典序排列");

    logAction("巡检文件日志器清单");
    YomkResponse fileResp = YomkAPI::LOGGER_INFO_LOGGERS("file");
    logResp("LOGGER_INFO_LOGGERS file", fileResp);
    auto fileArr = yomk::unPack<yomkmsg::StringArray>(fileResp.m_data);
    if (fileArr)
    {
        for (const auto& loggerName : fileArr->d)
        {
            logLoggerLine("LOGGER_INFO_LOGGERS", loggerName);
        }
    }
    logVerify("blackbox 与 shiftlog 在列：控制台与文件是两张独立的日志器表");

    logAction("单查班次存档器 shiftlog");
    YomkResponse resp = YomkAPI::LOGGER_INFO_LOGGER("shiftlog", "file");
    logResp("LOGGER_INFO_LOGGER shiftlog(file)", resp);
    auto shiftInfo = yomk::unPack<yomkmsg::LoggerInfo>(resp.m_data);
    if (shiftInfo)
    {
        logLoggerInfo("LOGGER_INFO_LOGGER", shiftInfo->d);
    }
    logVerify("type=file、dir 承载日志目录：m_msg 恒 \"ok\"，数据走 m_data 结构化包");

    logAction("单查未注册日志器 ghost");
    resp = YomkAPI::LOGGER_INFO_LOGGER("ghost", "console");
    logResp("LOGGER_INFO_LOGGER ghost(console)", resp);
    logVerify("未注册名字 -> eNo(logger not found.)：not-found 是框架统一惯例");

    logAction("全量导出日志器状态");
    resp = YomkAPI::LOGGER_INFO_ALL();
    logResp("LOGGER_INFO_ALL", resp);
    auto allArr = yomk::unPack<yomkmsg::LoggerInfoList>(resp.m_data);
    if (allArr)
    {
        for (const auto& info : allArr->d)
        {
            logLoggerInfo("LOGGER_INFO_ALL", info);
        }
    }
    logVerify("console/file 条目各自报逐器门控层真值（未做具名开关恒全 on）与对应侧代理态——两层归因的条目侧");

    // 全局主闸态单查：ALL 条目报逐器真值，主闸态经 LOGGER_INFO_GLOBAL(type) 按 type 分流独立承载，两层一起看全
    logAction("复查控制台侧全局主闸状态");
    resp = YomkAPI::LOGGER_INFO_GLOBAL("console");
    logResp("LOGGER_INFO_GLOBAL console", resp);
    auto gs = yomk::unPack<yomkmsg::LoggerState>(resp.m_data);
    if (gs)
    {
        std::ostringstream oss;
        oss << "[LOGGER_INFO_GLOBAL console] 主闸: debug=" << (gs->d.m_debugOn ? "on" : "off")
            << " info=" << (gs->d.m_infoOn ? "on" : "off") << " warn=" << (gs->d.m_warnOn ? "on" : "off")
            << " error=" << (gs->d.m_errorOn ? "on" : "off") << " proxy=" << (gs->d.m_proxyOn ? "on" : "off");
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
    logAction("复查文件侧全局主闸状态");
    resp = YomkAPI::LOGGER_INFO_GLOBAL("file");
    logResp("LOGGER_INFO_GLOBAL file", resp);
    auto fgs = yomk::unPack<yomkmsg::LoggerState>(resp.m_data);
    if (fgs)
    {
        std::ostringstream oss;
        oss << "[LOGGER_INFO_GLOBAL file] 文件主闸: debug=" << (fgs->d.m_debugOn ? "on" : "off")
            << " info=" << (fgs->d.m_infoOn ? "on" : "off") << " warn=" << (fgs->d.m_warnOn ? "on" : "off")
            << " error=" << (fgs->d.m_errorOn ? "on" : "off") << " proxy=" << (fgs->d.m_proxyOn ? "on" : "off");
        YOMK_INFO_TAG("arm.demo", oss.str());
    }
    logVerify("条目逐器真值 + 两侧主闸态独立单查：两套主闸/两套代理各自真实呈现，输出与落盘归因唯一（条目 off 是逐器层所致、条目 on 而无输出查对应侧主闸）");
}

// 场景 6：收班清档（DELETE 两表清理、磁盘保留、惰性重建）
static void showArmLogCleanup(const std::string& logDir)
{
    logScene(
        6,
        "收班清档",
        "FILE_LOG_DELETE + 惰性重建",
        "收班下线文件器与默认日志器：DELETE 按名同时清理 console+file 两表并返回"
        "各清了几条，磁盘 .log 文件保留（数据保全归调用方）；删除后再写自动重建。");

    logAction("清档安全黑匣子 blackbox", "FILE_LOG_DELETE blackbox", [&] { return YomkAPI::FILE_LOG_DELETE("blackbox"); });
    logAction("清档班次存档 shiftlog", "FILE_LOG_DELETE shiftlog", [&] { return YomkAPI::FILE_LOG_DELETE("shiftlog"); });
    logVerify("msg=deleted console:0 file:1：blackbox 只在 file 表，DELETE 报告两表各自清理数");

    logAction("清档未注册日志器 ghost", "FILE_LOG_DELETE ghost", [&] { return YomkAPI::FILE_LOG_DELETE("ghost"); });
    logVerify("两表均未命中 -> eNo(logger not found.)：not-found 是框架统一惯例");

    logAction("清档默认日志器 MainLogger", "FILE_LOG_DELETE MainLogger", [&] { return YomkAPI::FILE_LOG_DELETE("MainLogger"); });
    logVerify("msg=deleted console:1 file:0：MainLogger 只在 console 表（框架预创建）");

    logAction("回读班次存档文件确认保留");
    YOMK_INFO_TAG("arm.demo", "---- shiftlog.log 内容（日志器已删、磁盘仍在） ----");
    logFileContent(fs::path(logDir) / "shiftlog.log");
    logVerify("DELETE 只清内存表：磁盘 .log 文件保留，数据保全归调用方");

    logAction("再写默认日志（验证惰性重建）", "YOMK_INFO", [&] { return YOMK_INFO("MainLogger recreated on demand"); });
    logAction("复查控制台清单确认重建");
    YomkResponse resp = YomkAPI::LOGGER_INFO_LOGGERS("console");
    logResp("LOGGER_INFO_LOGGERS console", resp);
    auto consoleArr = yomk::unPack<yomkmsg::StringArray>(resp.m_data);
    if (consoleArr)
    {
        for (const auto& loggerName : consoleArr->d)
        {
            logLoggerLine("LOGGER_INFO_LOGGERS", loggerName);
        }
    }
    logVerify("MainLogger 回归清单：日志器按名惰性创建，无需手动注册；班次结束，arm.demo 叙事仍在");
}

int main(int argc, char* argv[])
{
    // 初始化框架（内置日志服务 /YomkLogger 随之自动启动，MainLogger 预创建）
    YomkAPI::init();

    YOMK_INFO_TAG("arm.demo", "机械臂产线 x YomkLogger 示例：一次班次贯穿 6 个场景，覆盖全部日志 API");
    YOMK_INFO_TAG("arm.demo",
        "日志按 tag 分层：arm.demo=叙事与结果 / MainLogger=默认日志器 / "
        "arm.joint=关节模块（演示自定义 tag）/ blackbox、shiftlog=文件器");

    // 日志目录：当前工作目录下的 YomkLog/（场景 3/4 创建文件日志器使用，目录由
    // 框架自动创建）。不解析 argv[0]——裸名/相对路径启动时 canonical 解析不到
    // 真实路径会抛 filesystem_error，直接用 cwd 即可在启动控制台所在目录落盘
    const std::string logDir = (fs::current_path() / "YomkLog").string();

    showArmLogLevels();
    showArmSilentMode();
    showArmBlackbox(logDir);
    showArmShiftLog(logDir);
    showArmLoggerDashboard();
    showArmLogCleanup(logDir);

    YOMK_INFO_TAG("arm.demo", "========== 示例结束：按回车退出 ==========");
    getchar();

    return 0;
}
