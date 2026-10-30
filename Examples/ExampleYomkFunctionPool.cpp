/**
 * @file ExampleYomkFunctionPool.cpp
 * @brief YomkFunctionPool 动态函数池示例：机械臂产线背景的 6 个场景小示例（面向初学者）
 *
 * 函数池是一个跨模块共享的扁平函数中心：把符合 YomkResponse(YomkPkgPtr) 签名的
 * 函数按名字注册进池子，之后在任意位置按名字调用——机械臂系统中主控、示教器、
 * 视觉工位正是这样共享无状态工具函数（负载合计、班次握手、位姿换算），彼此
 * 无需感知对方实现。同名重复注册即运行时热替换，无需先注销。
 *
 * 本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
 * 全部 6 个 YOMK_FUNCTIONPOOL_* 用户宏（REGISTER 含 2/3/4 参重载形式）无一遗漏。
 *
 * 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.tool 工具函数体），
 * 日志流即执行序、顺着读就能明白：每个操作先打"开始执行 xxx"——操作是用户视角
 * 的业务动作，随后结果行标注本次接口调用；CALL 在调用方线程同步执行被调函数，
 * 工具函数体的 arm.tool 日志被夹在开始行与结果行之间，归属一目了然。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmToolRegister  [YOMK_FUNCTIONPOOL_REGISTER(2参) / CALL]
 *        开班建工具柜：登记"负载合计"工具后按名调用，入参流进函数、结果流回
 *        调用方（msg 与 data 双份）——跨模块共享的最小闭环。
 * 场景 2 showArmToolMeta      [YOMK_FUNCTIONPOOL_REGISTER(4参) / INFO_NAME / CALL]
 *        带元数据登记班次握手工具：MsgName 与说明仅作内省元数据，调用时框架
 *        不做类型校验；INFO_NAME 单查逐字段确认。
 * 场景 3 showArmToolNullGuard [YOMK_FUNCTIONPOOL_CALL(nullptr)]
 *        空参守卫：CALL 传 nullptr，函数内解包宏判空自动返回 eNo——统一契约
 *        无需函数自己写 if；arm.tool 无日志自证函数体未执行。
 * 场景 4 showArmToolHotSwap   [YOMK_FUNCTIONPOOL_REGISTER(3参) / INFO_NAME / CALL]
 *        班中工具升级：同名再注册即热替换（无需注销），CALL 走新实现；MsgName
 *        与说明元数据随新注册整体刷新（未传说明则清空）。
 * 场景 5 showArmToolDashboard [INFO_NAMES / INFO_NAME / INFO_ALL]
 *        运维面板：函数名清单（字典序）、单查未注册名字演示 eNo not-found、
 *        全量逐字段导出——框架只回结构化数据，展示格式由用户定义。
 * 场景 6 showArmToolCleanup   [YOMK_FUNCTIONPOOL_UNREGISTER / CALL / INFO_ALL]
 *        收班清柜：注销后调用、重复注销均返回 eNo（not-found 惯例）；
 *        INFO_ALL 确认清单已空，班次闭环。
 *
 * 工具函数规划（跨场景流转，模拟多模块共享同一批无状态工具）：
 *   tool.payloadSum  2 参注册        三轴负载合计（入参 StringArray 各轴 kg，出参合计）
 *   tool.handshake   4 参注册        班次握手（入参 String 操作工名，出参问候语）
 *   tool.handshake   3 参再注册(V2)  热替换为升级版问候（场景 4）
 */

#include <functional>
#include <sstream>
#include <string>
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
// CALL 的返回值即被调函数的返回值——结果数据若挂 String 包则随行展示（如负载合计值）；
// 其余内省包（StringArray/FuncInfo/FuncInfoList）形态各异，解包类型不匹配时静默不展示
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
// 结果行标注本次接口调用；CALL 在调用方线程同步执行被调函数，工具函数体的
// arm.tool 日志被夹在开始行与结果行之间，归属一目了然，读者顺着日志读即可理解
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如 INFO_NAMES/INFO_NAME/INFO_ALL）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// 自省结果展开：单个函数 FuncInfo 的全部字段逐行展示（函数名行 + 字段行），
// INFO_NAME 与 INFO_ALL 共用；紧随其结果行输出，函数名行以 "-" 起头、字段行缩进对齐
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

// INFO_NAMES 清单行：函数名列表逐行展示（" - " 无序列表风格）
static void logFuncLine(const std::string& api, const std::string& funcName)
{
    YOMK_INFO_TAG("arm.demo", "[" + api + "] - " + funcName);
}

// 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
static void logVerify(const std::string& text)
{
    YOMK_INFO_TAG("arm.demo", "[验证] " + text);
}

// ---------------------------------------------------------------------------
// 可注册进函数池的工具函数：签名统一为 YomkResponse(YomkPkgPtr)，全部无状态。
// CALL 在调用方线程同步执行，函数体的 arm.tool 日志会先于调用方结果行出现。
// ---------------------------------------------------------------------------

// 负载合计工具：跨模块共享的无状态计算函数（场景 1 以 2 参形式登记）。
// 入参 StringArray 各轴负载（kg），合计结果同时放进 msg 与 m_data——调用方两者都能拿到
YomkResponse payloadSum(YomkPkgPtr pkg)
{
    // 解包入参：宏已自动判空，失败自动返回 {eNo, "错误信息"}（空参守卫见场景 3）
    YomkUnPackPkgResponse(pkg, StringArray, loads);

    std::ostringstream oss;
    oss << "负载合计工具执行：收到 " << loads->d.size() << " 轴负载（调用方线程同步执行）";
    YOMK_INFO_TAG("arm.tool", oss.str());

    int totalKg = 0;
    for (const auto& load : loads->d)
    {
        totalKg += std::stoi(load);
    }

    oss.str("");
    oss << "负载合计工具完成：payload=" << totalKg << "kg";
    YOMK_INFO_TAG("arm.tool", oss.str());

    // msg 描述结果，m_data 携带结果数据——调用方两者都能拿到
    return {YomkResponse::eOk, "payload=" + std::to_string(totalKg) + "kg", yomk::String_::shared(std::to_string(totalKg))};
}

// 班次握手工具：演示带元数据的登记与调用（场景 2 以 4 参形式登记，场景 4 被热替换）
YomkResponse handshake(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, String, worker);
    std::ostringstream oss;
    oss << "班次握手工具执行：向 " << worker->d << " 回执问候";
    YOMK_INFO_TAG("arm.tool", oss.str());
    return {YomkResponse::eOk, "shift hello, " + worker->d};
}

// ---------------------------------------------------------------------------
// 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
// 工具流转：payloadSum/handshake 场景 1/2 登记，handshake 场景 4 热替换，场景 6 清柜。
// ---------------------------------------------------------------------------

// 场景 1：开班建工具柜，主控模块登记负载合计工具并完成首次调用
static void showArmToolRegister()
{
    logScene(
        1,
        "开班登记工具函数",
        "YOMK_FUNCTIONPOOL_REGISTER(2参) / CALL",
        "开班时主控模块把负载合计工具按名登记进函数池：REGISTER 只登记不执行，"
        "CALL 才按名调用——入参消息包流进函数，返回值原样流回调用方，跨模块共享"
        "无需感知对方实现。");

    logAction(
        "登记负载合计工具 tool.payloadSum", "REGISTER tool.payloadSum",
        [&] { return YOMK_FUNCTIONPOOL_REGISTER("tool.payloadSum", payloadSum); });

    logAction(
        "调用负载合计工具 tool.payloadSum", "CALL tool.payloadSum",
        [&] { return YOMK_FUNCTIONPOOL_CALL("tool.payloadSum", yomk::StringArray_::shared(std::vector<std::string>{"12", "8", "5"})); });
    logVerify("arm.tool 日志夹在开始行与结果行之间——CALL 在调用方线程同步执行；返回值即函数返回值，msg 与 data 双份携带 payload=25kg");
}

// 场景 2：带元数据登记（4 参：MsgName + 说明）与单查确认
static void showArmToolMeta()
{
    logScene(
        2,
        "带元数据登记工具",
        "YOMK_FUNCTIONPOOL_REGISTER(4参) / INFO_NAME / CALL",
        "班次握手工具登记时声明期望消息类型与用途说明：MsgName 与说明仅作内省"
        "元数据，调用时框架不做类型校验；INFO_NAME 单查可逐字段读取元信息。");

    logAction(
        "登记班次握手工具 tool.handshake（带元数据）", "REGISTER tool.handshake (4参)",
        [&] { return YOMK_FUNCTIONPOOL_REGISTER("tool.handshake", handshake, String, "班次握手：向指定操作工回执问候"); });

    logAction("查看握手工具元信息");
    YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAME("tool.handshake");
    logResp("INFO_NAME tool.handshake", resp);
    YomkUnPackPkg(resp.m_data, FuncInfo, info);
    if (info)
    {
        logFuncInfo("INFO_NAME", info->d);
    }
    logVerify("msgName=String、description 随行：元数据仅供内省读阅，CALL 时框架不做类型校验");

    logAction(
        "调用班次握手工具 tool.handshake", "CALL tool.handshake",
        [&] { return YOMK_FUNCTIONPOOL_CALL("tool.handshake", yomk::String_::shared("张工")); });
}

// 场景 3：空参调用统一守卫
static void showArmToolNullGuard()
{
    logScene(
        3,
        "空参调用统一守卫",
        "YOMK_FUNCTIONPOOL_CALL(nullptr)",
        "误传空参调用握手工具：CALL 的入参消息包可为 nullptr，被调函数内的解包宏"
        "判空后自动返回 eNo——空参守卫是框架统一契约，无需函数自己写 if。");

    logAction(
        "空参调用握手工具 tool.handshake", "CALL tool.handshake (nullptr)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("tool.handshake", nullptr); });
    logVerify("开始行与结果行之间无 arm.tool 日志：函数体未执行——eNo 来自函数入口解包宏判空，统一契约无需函数自己写 if");
}

// 场景 4：同名热替换（班中工具升级）
static void showArmToolHotSwap()
{
    logScene(
        4,
        "工具函数热升级",
        "YOMK_FUNCTIONPOOL_REGISTER(3参) / INFO_NAME / CALL",
        "班中问候口径升级：对已注册名字再次 REGISTER 即热替换函数本体，无需先"
        "注销；MsgName 与说明元数据随新注册整体刷新，随后调用走新实现。");

    // lambda 先赋给具名变量再传宏：返回值构造含逗号时，
    // 直接内联进宏会让预处理器把逗号误当实参分隔符
    YomkServiceFunc handshakeV2 = [](YomkPkgPtr pkg) -> YomkResponse
    {
        YomkUnPackPkgResponse(pkg, String, worker);
        std::ostringstream oss;
        oss << "班次握手工具 V2 执行：向 " << worker->d << " 升级问候";
        YOMK_INFO_TAG("arm.tool", oss.str());
        return {YomkResponse::eOk, "shift hello V2, " + worker->d};
    };

    logAction(
        "热替换班次握手工具 tool.handshake（V2）", "REGISTER tool.handshake (V2, 3参)",
        [&] { return YOMK_FUNCTIONPOOL_REGISTER("tool.handshake", handshakeV2, String); });
    logVerify("already exist 提示 + eOk：同名重复注册即运行时热替换，无需先注销");

    logAction("复查握手工具元信息");
    YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAME("tool.handshake");
    logResp("INFO_NAME tool.handshake", resp);
    YomkUnPackPkg(resp.m_data, FuncInfo, info);
    if (info)
    {
        logFuncInfo("INFO_NAME", info->d);
    }
    logVerify("msgName 保持 String、说明未传被清空为(未指定)：元数据随新注册整体替换而非合并");

    logAction(
        "调用升级后的握手工具 tool.handshake", "CALL tool.handshake (V2)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("tool.handshake", yomk::String_::shared("张工")); });
    logVerify("arm.tool 日志为 V2 文案：热替换立即生效，旧实现不再被调用");
}

// 场景 5：产线运维面板巡检
static void showArmToolDashboard()
{
    logScene(
        5,
        "工具柜运维面板",
        "INFO_NAMES / INFO_NAME / INFO_ALL",
        "运维面板巡检：INFO_NAMES 拉取函数名清单；INFO_NAME 单查工具元信息"
        "（未注册名字演示 eNo）；INFO_ALL 全量导出——框架只回结构化数据，"
        "展示格式由用户定义。");

    logAction("巡检函数名清单");
    YomkResponse namesResp = YOMK_FUNCTIONPOOL_INFO_NAMES();
    logResp("INFO_NAMES", namesResp);
    YomkUnPackPkg(namesResp.m_data, StringArray, nameArr);
    if (nameArr)
    {
        for (const auto& funcName : nameArr->d)
        {
            logFuncLine("INFO_NAMES", funcName);
        }
    }
    logVerify("tool.handshake 与 tool.payloadSum 在列：清单按函数名字典序排列");

    logAction("单查未注册工具 tool.ghost");
    YomkResponse resp = YOMK_FUNCTIONPOOL_INFO_NAME("tool.ghost");
    logResp("INFO_NAME tool.ghost", resp);
    logVerify("未注册名字 -> eNo(funcName is not register)：not-found 是框架统一惯例");

    logAction("全量导出工具元信息");
    resp = YOMK_FUNCTIONPOOL_INFO_ALL();
    logResp("INFO_ALL", resp);
    YomkUnPackPkg(resp.m_data, FuncInfoList, allArr);
    if (allArr)
    {
        for (const auto& info : allArr->d)
        {
            logFuncInfo("INFO_ALL", info);
        }
    }
}

// 场景 6：收班清柜（UNREGISTER 与 not-found 惯例）
static void showArmToolCleanup()
{
    logScene(
        6,
        "收班清柜注销",
        "YOMK_FUNCTIONPOOL_UNREGISTER / CALL / INFO_ALL",
        "收班下线全部工具：UNREGISTER 把函数移出池子；注销后调用、重复注销均"
        "返回 eNo（not-found 惯例）——eInvalid 仅用于参数无效；INFO_ALL 确认清柜。");

    logAction(
        "注销负载合计工具 tool.payloadSum", "UNREGISTER tool.payloadSum",
        [&] { return YOMK_FUNCTIONPOOL_UNREGISTER("tool.payloadSum"); });

    logAction(
        "调用已注销工具 tool.payloadSum", "CALL tool.payloadSum (已注销)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("tool.payloadSum", nullptr); });
    logVerify("已注销名字 -> eNo(funcName is not register)：开始行与结果行之间无 arm.tool 日志，函数已不在池中");

    logAction(
        "重复注销负载合计工具 tool.payloadSum", "UNREGISTER tool.payloadSum (重复)",
        [&] { return YOMK_FUNCTIONPOOL_UNREGISTER("tool.payloadSum"); });
    logVerify("重复注销同样 -> eNo：not-found 是框架统一惯例，不报错也不崩溃");

    logAction(
        "注销班次握手工具 tool.handshake", "UNREGISTER tool.handshake",
        [&] { return YOMK_FUNCTIONPOOL_UNREGISTER("tool.handshake"); });

    logAction("巡检清单确认已清空");
    YomkResponse allResp = YOMK_FUNCTIONPOOL_INFO_ALL();
    logResp("INFO_ALL", allResp);
    YomkUnPackPkg(allResp.m_data, FuncInfoList, allArr);
    logVerify(allArr && allArr->d.empty() ? "函数清单已空：班次结束，工具柜清空，班次闭环" : "函数清单非空：存在未注销工具");
}

int main(int argc, char* argv[])
{
    // 初始化框架（函数池服务 /YomkFunctionPool 随之自动启动）
    YOMK_INIT();

    YOMK_INFO_TAG("arm.demo", "机械臂产线 x YomkFunctionPool 示例：一次班次贯穿 6 个场景，覆盖全部函数池 API");
    YOMK_INFO_TAG("arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.tool=工具函数体（调用方线程同步执行）");

    showArmToolRegister();
    showArmToolMeta();
    showArmToolNullGuard();
    showArmToolHotSwap();
    showArmToolDashboard();
    showArmToolCleanup();

    YOMK_INFO_TAG("arm.demo", "========== 示例结束：按回车退出 ==========");
    getchar();

    return 0;
}
