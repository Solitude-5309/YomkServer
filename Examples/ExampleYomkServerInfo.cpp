/**
 * @file ExampleYomkServerInfo.cpp
 * @brief YomkServerInfo 服务自省示例：机械臂产线背景的 6 个场景小示例（面向初学者）
 *
 * 服务自省即"运行期问框架"：/YomkServerInfo 是随 YOMK_INIT 自动启动的内置服务，
 * 提供服务清单、指定服务的功能函数清单、单函数元信息与全量拓扑四类查询——
 * 调试与运维时用它确认"产线上现在有哪些工位（服务）、每个功能函数期望什么
 * 消息类型"，无需翻源码即可掌握调用拓扑。
 *
 * 本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
 * 全部 4 个 YOMK_SERVER_INFO_* 用户宏无一遗漏（服务注册/请求/注销/关闭
 * 作为自省对象的素材接口随之演示）。
 *
 * 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.station
 * 装配工位函数体），日志流即执行序、顺着读就能明白：每个操作先打"开始执行
 * xxx"——操作是用户视角的业务动作，随后结果行标注本次接口调用；REQUEST
 * 在调用方线程同步执行工位函数，arm.station 日志被夹在开始行与结果行之间，
 * 归属一目了然。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmPlantRoster     [INFO_SERVICES / NEW_SERVICE / REQUEST]
 *        开班点名与工位上线：内置服务清单打底，NEW_SERVICE 注册装配工位后
 *        清单立即多一项；REQUEST 试运行 /report 与 /quality_check 证明注册
 *        即可调用（工位函数体 arm.station 日志夹在开始行与结果行之间）。
 * 场景 2 showArmStationFunctions [INFO_FUNCTIONS]
 *        工位职能清单：FuncInfoList 结构化清单（4 参注册的 /quality_check
 *        带 QualityCheckReq 元数据与函数说明，2 参注册的 /report 双空）；
 *        不存在的工位返回空清单 eOk，空服务名返回 eInvalid。
 * 场景 3 showArmFuncLookup      [INFO_FUNCTION]
 *        函数名片单查：命中返回结构化 FuncInfo 包（m_msg 恒 "ok"，
 *        m_msgName=期望消息类型名、m_description=函数说明）；4 种坏 URL 均
 *        返回 eNo（url 解析失败 / 目标不存在），是普通返回值不崩溃。
 * 场景 4 showArmPlantTopology   [INFO_ALL]
 *        全厂拓扑总览：ServerServiceInfoList 层级包逐服务逐函数导出——
 *        排障时一次看清"哪些工位在册、各自能干什么"。
 * 场景 5 showArmLiveView        [INFO_SERVICES / INFO_ALL / DEL_SERVICE]
 *        工位下线实时视图：DEL_SERVICE 后清单与拓扑立即少一项（无需重启），
 *        注销不存在的服务返回 -1——自省读的是运行期服务表。
 * 场景 6 showArmPowerOff        [SHUTDOWN / INFO_SERVICES]
 *        收班断电：SHUTDOWN 排空在途请求并释放全部服务，关闭后任何自省调用
 *        统一返回 eInvalid "YomkServer is not init"；日志宏随之静默——
 *        静默即演示效果，收班预告已提前说明，示例日志到此为止。
 *
 * 演示服务规划（机械臂产线装配工位，跨场景流转）：
 *   /ArmStation              NEW_SERVICE 注册（场景 1）、DEL_SERVICE 下线（场景 5）
 *   /report        2 参注册  工位状态上报（入参 String 状态文本，回执确认；无元数据）
 *   /quality_check 4 参注册  质量抽检（入参 QualityCheckReq；声明期望消息类型
 *                            与函数说明，供场景 2/3 的自省识别）
 */

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "YomkAPI.h"

// ---------------------------------------------------------------------------
// 辅助输出：全部走 YomkLogger 控制台日志（YOMK_INFO_TAG 等），无 std::cout。
// 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
// 格式敏感的行先用 ostringstream 拼好再单参输出，避免多参数自动空格
// 把 "ret=0" 拆成 "ret= 0"。
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
// 清单类返回挂 String data 时随行展示；结构化包（StringArray/FuncInfo/FuncInfoList/
// ServerServiceInfoList）形态各异，解包类型不匹配时静默不展示
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
    if (resp.m_status == YomkResponse::eOk && resp.m_data)
    {
        YomkUnPackPkg(resp.m_data, String, data);
        if (data)
        {
            oss << ", data=\"" << data->d << "\"";
        }
    }
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 操作日志（YomkResponse 版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
// 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
// 结果行标注本次接口调用；REQUEST 在调用方线程同步执行工位函数，arm.station
// 日志被夹在开始行与结果行之间，归属一目了然，读者顺着日志读即可理解
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如清单/拓扑/名片类查询）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// 操作日志（int 返回版）：NEW_SERVICE/DEL_SERVICE 等框架管理接口返回 int
// （0=成功，-1=失败），先打开始行再调用，结果行标注返回值
static void logActionInt(const std::string& op, const std::string& api, const std::function<int()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    int ret = call();
    std::ostringstream oss;
    oss << "[" << api << "] ret=" << ret << (ret == 0 ? "(成功)" : "(失败)");
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 服务清单行：StringArray 逐行展示（" - " 无序列表风格）
static void logStringLine(const std::string& api, const std::string& line)
{
    YOMK_INFO_TAG("arm.demo", "[" + api + "] - " + line);
}

// 自省结果展开：单个函数 FuncInfo 的全部字段逐行展示（函数名行 + 字段行），
// FUNCTIONS 清单与 FUNCTION 单查共用；紧随其结果行输出，函数名行以 "-" 起头、
// 字段行缩进对齐，元数据为空时兜底"(未指定)"
static void logFuncInfo(const std::string& api, const yomk::FuncInfo& info)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << info.m_funcName;
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     msgName: " << (info.m_msgName.empty() ? "(未指定)" : info.m_msgName);
    YOMK_INFO_TAG("arm.demo", oss.str());

    oss.str("");
    oss << "[" << api << "]     description: " << (info.m_description.empty() ? "(未指定)" : info.m_description);
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 拓扑展开：服务名行 + 该服务的函数清单行（函数名 + 可选 MsgName 标注），
// INFO_ALL 层级包逐服务逐函数导出（框架只回结构化数据，展示格式由用户定义）
static void logSrvLine(const std::string& api, const std::string& srvName)
{
    YOMK_INFO_TAG("arm.demo", "[" + api + "] - " + srvName);
}

static void logTopologyFunc(const std::string& api, const yomk::FuncInfo& info)
{
    // 二级函数行退两级空格：相对服务名行（"- " 前缀，内容起始差 2 字符）再深一层，
    // 层级差 4 字符在长日志前缀下依然分明
    std::string line = "      " + info.m_funcName;
    if (!info.m_msgName.empty())
    {
        line += " [" + info.m_msgName + "]";
    }
    YOMK_INFO_TAG("arm.demo", "[" + api + "]" + line);
}

static void logTopology(const std::string& api, const YomkResponse& resp)
{
    YomkUnPackPkg(resp.m_data, ServerServiceInfoList, list);
    if (!list)
    {
        return;
    }
    for (const auto& srv : list->d)
    {
        logSrvLine(api, srv.m_serviceName);
        for (const auto& func : srv.m_funcs)
        {
            logTopologyFunc(api, func);
        }
    }
}

// 拓扑摘要行：服务名 + 函数计数（工位下线后复查用，避免重复全量展开）
static void logSrvBrief(const std::string& api, const std::string& srvName, std::size_t funcCount)
{
    std::ostringstream oss;
    oss << "[" << api << "] - " << srvName << "（" << funcCount << " 个功能函数）";
    YOMK_INFO_TAG("arm.demo", oss.str());
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    YOMK_INFO_TAG("arm.demo", "[验证] " + text);
}

// ---------------------------------------------------------------------------
// 自省素材：机械臂产线装配工位服务。2 参注册的 /report 无元数据，4 参注册的
// /quality_check 声明期望消息类型 QualityCheckReq 并附函数说明——差异将由
// 场景 2/3 的自省结果直接呈现。REQUEST 在调用方线程同步执行函数体，
// arm.station 日志夹在调用方开始行与结果行之间，归属一目了然。
// ---------------------------------------------------------------------------

// 质量抽检请求：此处仅作 /quality_check 的期望消息类型元数据，供自省识别
struct QualityCheckReq
{
    std::string m_station;  // 工位号
    std::int32_t m_count;   // 抽检件数
};

YomkMsg(QualityCheckReq, QualityCheckReq, req)

class ArmStationService : public YomkService
{
public:
    explicit ArmStationService(YomkServer* server) : YomkService(server)
    {
        name("/ArmStation");  // 服务名：URL 前缀，必须 / 开头且全局唯一
    }

    int init() override
    {
        // 2 参注册：无元数据；4 参注册：声明 /quality_check 期望 QualityCheckReq
        // 并附函数说明，供自省识别
        installFunc("/report", &ArmStationService::report);
        installFunc("/quality_check", &ArmStationService::qualityCheck, "QualityCheckReq",
                    "装配工位质量抽检：按件抽检并回执结论");
        YOMK_INFO_TAG("arm.station", "init: install /report (no meta), /quality_check [QualityCheckReq] + description");
        return 0;
    }

    // 工位状态上报：收到状态文本回执确认
    YomkResponse report(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, String, status);
        std::ostringstream oss;
        oss << "工位状态上报：收到 \"" << status->d << "\"";
        YOMK_INFO_TAG("arm.station", oss.str());
        return {YomkResponse::eOk, "station report received: " + status->d};
    }

    // 质量抽检：解包 QualityCheckReq，按件抽检并回执结论
    YomkResponse qualityCheck(YomkPkgPtr pkg)
    {
        YomkUnPackPkgResponse(pkg, QualityCheckReq, data);
        std::ostringstream oss;
        oss << "质量抽检执行：工位 " << data->req.m_station << " 抽检 " << data->req.m_count << " 件";
        YOMK_INFO_TAG("arm.station", oss.str());
        return {YomkResponse::eOk,
            "quality check passed: " + data->req.m_station + ", " + std::to_string(data->req.m_count) + " pcs"};
    }
};

// ---------------------------------------------------------------------------
// 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
// 工位流转：/ArmStation 场景 1 上线，场景 2/3/4 自省读它，场景 5 下线，场景 6 断电。
// ---------------------------------------------------------------------------

// 场景 1：开班点名与工位上线（注册 + 试运行）
static void showArmPlantRoster()
{
    logScene(
        1,
        "开班点名与工位上线",
        "INFO_SERVICES / NEW_SERVICE / REQUEST",
        "开班点名看产线在册工位：INFO_SERVICES 查到的就是框架运行期服务表；"
        "NEW_SERVICE 把装配工位注册进产线，REQUEST 试运行两个功能函数证明"
        "注册即可调用。");

    logAction("开班点名：查询产线在册服务");
    YomkResponse namesResp = YOMK_SERVER_INFO_SERVICES();
    logResp("INFO_SERVICES", namesResp);
    YomkUnPackPkg(namesResp.m_data, StringArray, nameArr);
    if (nameArr)
    {
        for (const auto& srvName : nameArr->d)
        {
            logStringLine("INFO_SERVICES", srvName);
        }
    }
    logVerify("5 项即内置服务（/YomkContext /YomkEventLoop /YomkFunctionPool /YomkLogger /YomkServerInfo），自省服务开箱即用无需注册");

    logActionInt("装配工位上线：注册 /ArmStation", "NEW_SERVICE /ArmStation", [] { return YOMK_NEW_SERVICE(ArmStationService); });

    logAction("复查在册服务清单");
    namesResp = YOMK_SERVER_INFO_SERVICES();
    logResp("INFO_SERVICES", namesResp);
    YomkUnPackPkg(namesResp.m_data, StringArray, nameArr2);
    if (nameArr2)
    {
        for (const auto& srvName : nameArr2->d)
        {
            logStringLine("INFO_SERVICES", srvName);
        }
    }
    logVerify("清单多一项 /ArmStation 且排最前：服务名按字典序排列，注册即时反映到下一次查询");

    logAction(
        "试运行工位功能：状态上报", "REQUEST /ArmStation/report",
        [&] { return YOMK_REQUEST("/ArmStation/report", yomk::String_::shared("joint ready")); });

    // 入参先赋具名变量：聚合初始化含逗号，直接内联进宏会被预处理器误当实参分隔符
    QualityCheckReq checkReq{"S3", 5};
    logAction(
        "试运行工位功能：质量抽检", "REQUEST /ArmStation/quality_check",
        [&] { return YOMK_REQUEST("/ArmStation/quality_check", yomk::QualityCheckReq_::shared(checkReq)); });
    logVerify("arm.station 日志夹在开始行与结果行之间——REQUEST 在调用方线程同步执行；返回值即函数返回值，msg 携带回执");
}

// 场景 2：工位职能清单（INFO_FUNCTIONS 三分支）
static void showArmStationFunctions()
{
    logScene(
        2,
        "工位职能清单",
        "INFO_FUNCTIONS",
        "查阅装配工位能干什么：INFO_FUNCTIONS 返回 FuncInfoList 结构化清单"
        "（m_funcName/m_msgName/m_description）——4 参注册的 /quality_check 带"
        "元数据，2 参注册的 /report 双空，差异一目了然。");

    logAction("查阅装配工位职能清单");
    YomkResponse funcsResp = YOMK_SERVER_INFO_FUNCTIONS("/ArmStation");
    logResp("INFO_FUNCTIONS /ArmStation", funcsResp);
    YomkUnPackPkg(funcsResp.m_data, FuncInfoList, listPkg);
    if (listPkg)
    {
        for (const auto& func : listPkg->d)
        {
            logFuncInfo("INFO_FUNCTIONS", func);
        }
    }
    logVerify("msgName 即 installFunc 第三参声明的期望消息类型、description 即第四参函数说明；2 参注册的 /report 两字段为(未指定)");

    logAction("查阅不存在工位 /GhostStation 的职能清单");
    funcsResp = YOMK_SERVER_INFO_FUNCTIONS("/GhostStation");
    logResp("INFO_FUNCTIONS /GhostStation", funcsResp);
    logVerify("服务不存在 -> 空清单 eOk：查询失败不崩溃，仍是普通返回值");

    logAction("以空服务名查阅职能清单");
    funcsResp = YOMK_SERVER_INFO_FUNCTIONS("");
    logResp("INFO_FUNCTIONS (空服务名)", funcsResp);
    logVerify("空服务名 -> eInvalid(srvName is empty)：参数无效与目标不存在是两种不同状态");
}

// 场景 3：功能函数名片单查（INFO_FUNCTION 命中契约 + 4 种坏 URL）
static void showArmFuncLookup()
{
    logScene(
        3,
        "功能函数名片单查",
        "INFO_FUNCTION",
        "按 URL 调阅单个函数的名片：命中返回结构化 FuncInfo 包（m_msg 恒 ok），"
        "msgName 与 description 承载注册时声明的元数据；url 解析失败与目标"
        "不存在均为 eNo。");

    logAction("调阅质量抽检函数名片");
    YomkResponse resp = YOMK_SERVER_INFO_FUNCTION("/ArmStation/quality_check");
    logResp("INFO_FUNCTION /ArmStation/quality_check", resp);
    YomkUnPackPkg(resp.m_data, FuncInfo, info);
    if (info)
    {
        logFuncInfo("INFO_FUNCTION", info->d);
    }
    logVerify("命中契约：m_msg 恒 ok、数据走 m_data；m_msgName=QualityCheckReq、m_description=第四参说明——与场景 2 清单同族");

    logAction("调阅状态上报函数名片");
    resp = YOMK_SERVER_INFO_FUNCTION("/ArmStation/report");
    logResp("INFO_FUNCTION /ArmStation/report", resp);
    YomkUnPackPkg(resp.m_data, FuncInfo, reportInfo);
    if (reportInfo)
    {
        logFuncInfo("INFO_FUNCTION", reportInfo->d);
    }
    logVerify("2 参注册的 /report msgName 为(未指定)：元数据是可选声明，不影响调用");

    logAction("以不以 / 开头的 url 单查", "INFO_FUNCTION ArmStation/quality_check (无/)",
        [&] { return YOMK_SERVER_INFO_FUNCTION("ArmStation/quality_check"); });

    logAction("以缺函数名的 url 单查", "INFO_FUNCTION /ArmStation (缺函数名)",
        [&] { return YOMK_SERVER_INFO_FUNCTION("/ArmStation"); });

    logAction("以服务不存在的 url 单查", "INFO_FUNCTION /GhostStation/hi (服务不存在)",
        [&] { return YOMK_SERVER_INFO_FUNCTION("/GhostStation/hi"); });

    logAction("以函数不存在的 url 单查", "INFO_FUNCTION /ArmStation/ghost (函数不存在)",
        [&] { return YOMK_SERVER_INFO_FUNCTION("/ArmStation/ghost"); });
    logVerify("4 种坏 URL 均为 eNo 且 msg 说明原因：url 解析失败 / 目标不存在是普通返回值，不报错不崩溃");
}

// 场景 4：全厂拓扑总览（INFO_ALL）
static void showArmPlantTopology()
{
    logScene(
        4,
        "全厂拓扑总览",
        "INFO_ALL",
        "一次拉取全厂服务拓扑：INFO_ALL 返回 ServerServiceInfoList 层级包"
        "（m_serviceName 服务名 + m_funcs 函数元信息列表），排障时一眼看清"
        "调用拓扑。");

    logAction("导出全厂服务拓扑");
    YomkResponse allResp = YOMK_SERVER_INFO_ALL();
    logResp("INFO_ALL", allResp);
    logTopology("INFO_ALL", allResp);
    logVerify("6 个服务 = 5 个内置 + 1 个装配工位，函数名带 / 前缀、带元数据的附 [MsgName] 标注——层级自上而下逐服务展开");
}

// 场景 5：工位下线实时视图（DEL_SERVICE + 自省复查）
static void showArmLiveView()
{
    logScene(
        5,
        "工位下线实时视图",
        "INFO_SERVICES / INFO_ALL / DEL_SERVICE",
        "班中工位检修下线：DEL_SERVICE 把工位移出服务表，清单与拓扑立即少"
        "一项，无需重启或刷新——自省读的是运行期服务表，注册/删除即时反映"
        "到下一次查询。");

    logActionInt("装配工位下线：注销 /ArmStation", "DEL_SERVICE /ArmStation", [] { return YOMK_DEL_SERVICE("/ArmStation"); });

    logAction("复查在册服务清单");
    YomkResponse namesResp = YOMK_SERVER_INFO_SERVICES();
    logResp("INFO_SERVICES", namesResp);
    YomkUnPackPkg(namesResp.m_data, StringArray, nameArr);
    if (nameArr)
    {
        for (const auto& srvName : nameArr->d)
        {
            logStringLine("INFO_SERVICES", srvName);
        }
    }
    logVerify("清单回到 5 项内置服务：删除即时生效，无需重启");

    logAction("复查全厂拓扑终态");
    YomkResponse allResp = YOMK_SERVER_INFO_ALL();
    logResp("INFO_ALL", allResp);
    YomkUnPackPkg(allResp.m_data, ServerServiceInfoList, list);
    if (list)
    {
        for (const auto& srv : list->d)
        {
            logSrvBrief("INFO_ALL", srv.m_serviceName, srv.m_funcs.size());
        }
    }
    logVerify("拓扑只剩 5 个内置服务，各服务函数计数如实呈现：工位已从调用拓扑消失");

    logActionInt("注销不存在的工位 /GhostStation", "DEL_SERVICE /GhostStation", [] { return YOMK_DEL_SERVICE("/GhostStation"); });
    logVerify("不存在的服务 -> ret=-1（失败）：注销失败是普通返回值不崩溃（框架侧另有直出提示行）");
}

// 场景 6：收班断电（SHUTDOWN 后自省守卫）
static void showArmPowerOff()
{
    logScene(
        6,
        "收班断电",
        "SHUTDOWN / INFO_SERVICES",
        "收班断电前先预告：SHUTDOWN 排空在途请求并释放全部服务（幂等），"
        "关闭后框架单例清空，任何自省调用统一返回 eInvalid——本条守卫演示"
        "将在静默中完成，静默即演示效果。");

    YOMK_INFO_TAG("arm.demo", "收班预告：SHUTDOWN 后框架已关闭，此后任何自省调用统一返回 eInvalid \"YomkServer is not init\"；");
    YOMK_INFO_TAG("arm.demo", "本示例所有日志宏随之静默——下面的静默段正是演示效果，示例日志至此结束，程序停在 getchar 等待回车退出");

    logAction(
        "收班断电：优雅关闭框架", "SHUTDOWN + INFO_SERVICES (关闭后守卫)",
        [&] {
            YOMK_SHUTDOWN();
            return YOMK_SERVER_INFO_SERVICES();  // eInvalid：结果行随日志宏静默，行为已由收班预告说明
        });
}

int main(int argc, char* argv[])
{
    // 初始化框架（/YomkServerInfo 等 5 个内置服务随之自动启动，自省开箱即用）
    YOMK_INIT();

    YOMK_INFO_TAG("arm.demo", "机械臂产线 x YomkServerInfo 示例：一次班次贯穿 6 个场景，覆盖全部服务自省 API");
    YOMK_INFO_TAG("arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.station=装配工位服务函数体（REQUEST 调用方线程同步执行）");

    showArmPlantRoster();
    showArmStationFunctions();
    showArmFuncLookup();
    showArmPlantTopology();
    showArmLiveView();
    showArmPowerOff();

    // 场景 6 已 SHUTDOWN：日志宏全部静默，结束横幅不再可见——
    // 结束预告已并入场景 6 的收班预告锚点，程序在此等待回车退出
    getchar();

    return 0;
}
