/**
 * @file ExampleYomkService.cpp
 * @brief YomkService 服务定义与调用示例：机械臂控制背景的 8 个场景小示例（面向初学者）
 *
 * "一切皆服务"：把业务逻辑封装为 YomkService 子类，注册到框架后任意位置用
 * URL "/服务名/函数名" 同步或异步调用。框架自动完成路由、弱绑定安全（服务
 * 删除后回调失效不崩溃）与调用拓扑自省——机械臂控制柜里主控、运动控制、
 * 留痕黑匣子正是这样协作的独立工位。
 *
 * 多服务工程结构（组织方式本身也是教程）：
 * - msgs/ArmMsgs.h             消息包定义（服务间传输的数据结构）
 * - services/MotionService     运动服务（被 main 直接请求，内部跨服务调留痕）
 * - services/TelemetryService  留痕服务（被 MotionService 调用，含弱绑定回调）
 * - boot/ArmBoot               启动编排（before -> start -> after 三阶段）
 *
 * 本示例按"控制柜一次上电会话"贯穿 8 个场景：每个场景是一个 showXXX 小函数，
 * 形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；服务
 * 生命周期与请求类用户接口全部覆盖（自省细节另见 ExampleYomkServerInfo）。
 *
 * 日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.boot 启动编排 /
 * arm.motion 运动服务函数体 / arm.telemetry 留痕服务函数体），日志流即执行序、
 * 顺着读就能明白：每个操作先打"开始执行 xxx"——操作是用户视角的业务动作，
 * 随后结果行标注本次接口调用；同步 REQUEST/CALL 在调用方线程执行服务函数体，
 * 服务日志被夹在开始行与结果行之间，归属一目了然。
 *
 * 小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：
 *
 * 场景 1 showArmPowerOnRegister [VERSION / SERVER_PTR / NEW_SERVICE / BOOT / INFO_SERVICES]
 *        控制柜上电与工位注册：框架版本与全局单例自检；NEW_SERVICE 一步创建
 *        并注册运动服务；BOOT 三阶段编排注册留痕服务（arm.boot 日志夹在开始
 *        行与结果行之间，before→start→after 顺序可见）；INFO_SERVICES 复查
 *        在册清单（2 个业务工位排最前 + 5 个内置服务）。
 * 场景 2 showArmLinkCheck      [REQUEST]
 *        通讯自检与寻址异常：REQUEST 同步回环拿到回包 data；请求不存在的
 *        服务/函数分别得到 service/function not found——eNo 是普通返回值
 *        不崩溃。
 * 场景 3 showArmAsyncTeach     [ASYNC_REQUEST]
 *        异步下发示教任务：发送即返回不阻塞控制回路，回调稍后在框架异步
 *        线程池执行——主线程与池线程 id 对比自证。
 * 场景 4 showArmMoveChain      [REQUEST / 成员 request() / 强类型回包]
 *        运动指令调用链：一次 REQUEST 触发 main→/move→/record 调用链
 *        （arm.motion/arm.telemetry 日志夹在中间），回包 m_data 携带强类型
 *        MoveResp——m_data 不止能传 String。
 * 场景 5 showArmIntrospect     [INFO_FUNCTIONS / INFO_FUNCTION]
 *        自省读取工位职能：4 参注册的 /move 带 MoveCmd 元数据与函数说明、
 *        2 参注册的 /echo 双空；INFO_FUNCTION 按 URL 调阅单张函数名片
 *        （内省细节详见 ExampleYomkServerInfo）。
 * 场景 6 showArmStationUpgrade [FUNCTIONPOOL_CALL / ADD_SERVICE]
 *        留痕工位升级：同名 ADD_SERVICE 即替换——替换前后直调钩子
 *        telemetry_hook（diag check V1→V2），[实例#N] 日志自证服务与
 *        钩子一并换新。
 * 场景 7 showArmStationOffline [DEL_SERVICE / FUNCTIONPOOL_CALL / REQUEST]
 *        留痕工位下线：DEL_SERVICE 删除即停——弱绑定钩子立即失效（eNo 不
 *        悬垂）、move 因留痕缺失失败沿调用链传播；重复注销返回 -1。
 * 场景 8 showArmPowerOff       [INFO_ALL / SHUTDOWN]
 *        收班断电：INFO_ALL 看终态拓扑；预告后 SHUTDOWN 排空在途请求并
 *        释放全部服务，关闭后任何请求统一返回 eInvalid——日志宏随之静默，
 *        静默段正是演示效果，示例日志到此为止。
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
 *   YomkInstallFunc(2/4 参) / weakFunc(&Class::Method) / 成员 request  服务侧 API
 */

#include <chrono>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

#include "YomkAPI.h"
#include "boot/ArmBoot.h"
#include "msgs/ArmMsgs.h"
#include "services/MotionService.h"
#include "services/TelemetryService.h"

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
// 回包挂 String data 时随行展示；结构化包（StringArray/FuncInfo/FuncInfoList/MoveResp）
// 形态各异，解包类型不匹配时静默不展示
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
// 结果行标注本次接口调用；同步 REQUEST/CALL 在调用方线程执行服务函数体，
// arm.motion/arm.telemetry 等服务日志被夹在开始行与结果行之间，归属一目了然
static YomkResponse logAction(const std::string& op, const std::string& api, const std::function<YomkResponse()>& call)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
    YomkResponse resp = call();
    logResp(api, resp);
    return resp;
}

// 操作开始行（返回值需要定制展开的动作，如清单/拓扑类查询与异步请求）：
// 由调用处随后打结果行
static void logAction(const std::string& op)
{
    YOMK_INFO_TAG("arm.demo", "开始执行 " + op);
}

// 操作日志（int 返回版）：NEW_SERVICE/ADD_SERVICE/DEL_SERVICE/BOOT 等框架管理
// 接口返回 int（0=成功，-1=失败），先打开始行再调用，结果行标注返回值
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

// 拓扑摘要行：服务名 + 函数计数（收班终态用，拓扑全量展开详见 ExampleYomkServerInfo）
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
// 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按上电会话顺序编排。
// 工位流转：/MotionService 场景 1 注册，场景 2/3/4/5 请求与自省读它；
// /TelemetryService 场景 1 经 BOOT 注册，场景 6 同名替换、场景 7 下线；
// 场景 8 断电。
// ---------------------------------------------------------------------------

// 场景 1：控制柜上电与工位注册（自检 + NEW_SERVICE + BOOT + 在册清单）
static void showArmPowerOnRegister()
{
    logScene(
        1,
        "控制柜上电与工位注册",
        "VERSION / SERVER_PTR / NEW_SERVICE / BOOT / INFO_SERVICES",
        "控制柜上电先自检（框架版本、全局单例就绪），再把业务工位注册进框架："
        "NEW_SERVICE 一步创建并注册运动服务；BOOT 按 before→start→after 三阶段"
        "编排注册留痕服务；INFO_SERVICES 复查在册清单。");

    logAction("控制柜上电自检：读取框架版本与全局单例");
    std::ostringstream oss;
    oss << "[YOMK_VERSION] version=" << YOMK_VERSION;
    YOMK_INFO_TAG("arm.demo", oss.str());
    oss.str("");
    oss << "[YOMK_SERVER_PTR] instance=" << (YOMK_SERVER_PTR ? "ready" : "null");
    YOMK_INFO_TAG("arm.demo", oss.str());
    logVerify("YOMK_INIT 后单例就绪：YOMK_SERVER_PTR 可判空，YOMK_SERVER_P 供服务构造时取 server 指针");

    logActionInt(
        "运动工位上线：一步创建并注册 /MotionService", "NEW_SERVICE /MotionService",
        [] { return YOMK_NEW_SERVICE(MotionService); });
    logVerify("返回 0 即注册成功：URL 前缀 /MotionService 生效，init() 已被框架调用（arm.motion 的 init 日志为证）");

    logActionInt(
        "启动编排注册留痕工位 /TelemetryService", "BOOT ArmBoot",
        [] { return YOMK_BOOT(new ArmBoot({"/TelemetryService"})); });
    logVerify("arm.boot 三段日志夹在开始行与结果行之间：BOOT 按 before→start→after 顺序执行");

    logAction("复查控制柜在册服务清单");
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
    logVerify("清单 7 项 = 2 个业务工位（排最前）+ 5 个内置服务，按字典序排列");
}

// 场景 2：通讯自检与寻址异常（REQUEST 回环 + not-found 分支）
static void showArmLinkCheck()
{
    logScene(
        2,
        "通讯自检与寻址异常",
        "REQUEST",
        "主控与运动工位做通讯自检：REQUEST 同步请求 /MotionService/echo，回包 "
        "data 随行；再故意请求不存在的服务与函数——not-found 是普通返回值，"
        "调用失败不崩溃。");

    logAction(
        "通讯自检：请求运动服务回环", "REQUEST /MotionService/echo",
        [&] { return YOMK_REQUEST("/MotionService/echo", YomkMkPtr(String, "ping")); });
    logVerify("arm.motion 日志夹在开始行与结果行之间——REQUEST 在调用方线程同步执行；回包 data 携带回显文本");

    logAction(
        "请求不存在的服务 /GhostService", "REQUEST /GhostService/echo",
        [&] { return YOMK_REQUEST("/GhostService/echo", YomkMkPtr(String, "ping")); });

    logAction(
        "请求运动服务不存在的函数", "REQUEST /MotionService/not_exist",
        [&] { return YOMK_REQUEST("/MotionService/not_exist", YomkMkPtr(String, "ping")); });
    logVerify("两种 not-found 均为 eNo 且 msg 说明原因：寻址失败是普通返回值，不报错不崩溃");
}

// 场景 3：异步下发示教任务（ASYNC_REQUEST 线程自证）
static void showArmAsyncTeach()
{
    logScene(
        3,
        "异步下发示教任务",
        "ASYNC_REQUEST",
        "示教任务走异步通道：ASYNC_REQUEST 发送即返回、不阻塞控制回路，回调"
        "稍后在框架异步线程池执行——主线程与池线程 id 对比自证。");

    std::ostringstream oss;
    oss << "主线程 id: " << std::this_thread::get_id();
    YOMK_INFO_TAG("arm.demo", oss.str());

    // 回调先赋具名变量再传宏：函数体含逗号时直接内联进宏会被预处理器误当实参分隔符
    YomkResponseFunc onTeachDone = [](YomkResponse resp)
    {
        std::ostringstream oss;
        oss << "[ASYNC_REQUEST 回调] status=" << resp.m_status << ", msg=\"" << resp.m_msg
            << "\"（池线程 id: " << std::this_thread::get_id() << "）";
        YOMK_INFO_TAG("arm.demo", oss.str());
    };

    logAction("异步下发示教点自检请求");
    YOMK_ASYNC_REQUEST("/MotionService/echo", YomkMkPtr(String, "teach point #1"), onTeachDone);
    logVerify("ASYNC_REQUEST 无同步结果行：发送即返回，控制回路不等回调");

    // 等待异步回调送达，保证回调日志先于下一条验证行出现、可读
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    logVerify("池线程 id 不同于主线程：请求在框架内置线程池并发执行，不阻塞控制回路");
}

// 场景 4：运动指令调用链（跨服务调用 + 强类型回包）
static void showArmMoveChain()
{
    logScene(
        4,
        "运动指令调用链",
        "REQUEST / 成员 request() / 强类型回包",
        "下发运动指令：一次 REQUEST 触发调用链 main→/move→/record——运动服务"
        "内部用成员 request() 跨服务调留痕工位（与全局宏等价、无需单例）；"
        "到位回执用自定义 MoveResp 强类型回包。");

    // 入参先赋具名变量：聚合初始化含逗号，直接内联进宏会被预处理器误当实参分隔符
    MoveCmd moveCmd{100.0, 50.0, 200.0, 0.8};
    YomkResponse moveResp = logAction(
        "下发运动指令：运动到 (100.0, 50.0, 200.0)mm", "REQUEST /MotionService/move",
        [&] { return YOMK_REQUEST("/MotionService/move", YomkMkPtr(MoveCmd, moveCmd)); });

    logAction("展开到位回执 MoveResp");
    std::ostringstream oss;
    YomkUnPackPkg(moveResp.m_data, MoveResp, respPkg);
    if (respPkg)
    {
        oss << "[REQUEST /MotionService/move] | MoveResp 到位 (" << respPkg->resp.m_reachX << ", "
            << respPkg->resp.m_reachY << ", " << respPkg->resp.m_reachZ << ")mm";
    }
    else
    {
        oss << "[REQUEST /MotionService/move] | (no MoveResp data)";
    }
    YOMK_INFO_TAG("arm.demo", oss.str());
    logVerify("arm.motion 与 arm.telemetry 日志夹在开始行与结果行之间：调用链 main→/move→/record 同步执行；m_data 携带强类型 MoveResp");
}

// 场景 5：自省读取工位职能（简要展示，细节见 ExampleYomkServerInfo）
static void showArmIntrospect()
{
    logScene(
        5,
        "自省读取工位职能",
        "INFO_FUNCTIONS / INFO_FUNCTION",
        "运维面板读取运动工位职能：INFO_FUNCTIONS 拉取函数清单（FuncInfoList "
        "结构化），INFO_FUNCTION 按 URL 调阅单张函数名片——4 参注册的 /move "
        "带元数据与说明，2 参注册的 /echo 双空（内省细节详见 "
        "ExampleYomkServerInfo）。");

    logAction("查阅运动工位职能清单");
    YomkResponse funcsResp = YOMK_SERVER_INFO_FUNCTIONS("/MotionService");
    logResp("INFO_FUNCTIONS /MotionService", funcsResp);
    YomkUnPackPkg(funcsResp.m_data, FuncInfoList, listPkg);
    if (listPkg)
    {
        for (const auto& func : listPkg->d)
        {
            logFuncInfo("INFO_FUNCTIONS", func);
        }
    }
    logVerify("msgName 即 YomkInstallFunc 第三参声明的期望消息类型、description 即第四参函数说明；2 参注册的 /echo 两字段为(未指定)");

    logAction("调阅运动指令函数名片");
    YomkResponse resp = YOMK_SERVER_INFO_FUNCTION("/MotionService/move");
    logResp("INFO_FUNCTION /MotionService/move", resp);
    YomkUnPackPkg(resp.m_data, FuncInfo, info);
    if (info)
    {
        logFuncInfo("INFO_FUNCTION", info->d);
    }
    logVerify("命中契约：m_msg 恒 ok、元数据走 m_data");
}

// 场景 6：留痕工位升级（同名替换，服务与钩子一并热切换）
static void showArmStationUpgrade()
{
    logScene(
        6,
        "留痕工位升级",
        "FUNCTIONPOOL_CALL / ADD_SERVICE",
        "班中留痕工位升级换新：同名 ADD_SERVICE 即替换，旧实例下线、新实例"
        "接管并重挂同名钩子——替换前后直调钩子（diag check V1→V2）自证服务"
        "与钩子一并替换。");

    logAction(
        "直调留痕钩子（替换前）", "FUNCTIONPOOL_CALL telemetry_hook (V1)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("telemetry_hook", YomkMkPtr(String, "diag check V1")); });
    logVerify("留痕记录 [实例#1]：diag check V1——钩子由当前实例 init 注册、按名直调");

    logActionInt(
        "留痕工位升级：同名注册替换 /TelemetryService", "ADD_SERVICE /TelemetryService",
        [] { return YOMK_ADD_SERVICE(new TelemetryService(YOMK_SERVER_P), "/TelemetryService"); });
    logVerify("框架直出 service already exists -> update to current service：旧实例 markDeleted+deinit，实例 #1 随即析构");
    logVerify("新实例 init 重挂 telemetry_hook，函数池直出 already exist -> update to current function：服务与钩子一并换新");

    logAction(
        "直调留痕钩子（替换后）", "FUNCTIONPOOL_CALL telemetry_hook (V2)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("telemetry_hook", YomkMkPtr(String, "diag check V2")); });
    logVerify("留痕记录 [实例#2]：diag check V2——旧实例已删除而钩子仍可用，钩子确已换新");
}

// 场景 7：留痕工位下线（删除即停与断链传播）
static void showArmStationOffline()
{
    logScene(
        7,
        "留痕工位下线",
        "DEL_SERVICE / FUNCTIONPOOL_CALL / REQUEST",
        "班末留痕工位下线：DEL_SERVICE 删除即停——弱绑定钩子立即失效，运动"
        "调用链因留痕缺失断链传播；注销失败是普通返回值。");

    logActionInt(
        "留痕工位下线：注销 /TelemetryService", "DEL_SERVICE /TelemetryService",
        [] { return YOMK_DEL_SERVICE("/TelemetryService"); });

    logActionInt(
        "重复注销已下线工位", "DEL_SERVICE /TelemetryService (重复)",
        [] { return YOMK_DEL_SERVICE("/TelemetryService"); });
    logVerify("服务已不存在 -> ret=-1：注销失败是普通返回值，不报错不崩溃");

    logAction(
        "直调留痕钩子（已下线）", "FUNCTIONPOOL_CALL telemetry_hook (已下线)",
        [&] { return YOMK_FUNCTIONPOOL_CALL("telemetry_hook", YomkMkPtr(String, "diag check V2")); });
    logVerify("钩子返回 eNo(service has been deleted or unregistered)：弱绑定失效，删除即停不悬垂");

    MoveCmd moveCmd{100.0, 50.0, 200.0, 0.8};
    logAction(
        "重发运动指令（留痕缺失）", "REQUEST /MotionService/move",
        [&] { return YOMK_REQUEST("/MotionService/move", YomkMkPtr(MoveCmd, moveCmd)); });
    logVerify("move 返回 eNo(move failed: ...)：留痕是运动的依赖前置，失败沿调用链传播");
}

// 场景 8：收班断电（INFO_ALL 终态 + SHUTDOWN 后守卫）
static void showArmPowerOff()
{
    logScene(
        8,
        "收班断电",
        "INFO_ALL / SHUTDOWN",
        "收班先看终态拓扑，再断电：SHUTDOWN 先停异步请求池（拒新→排空→join），"
        "再逐个 deinit 并释放全部服务（幂等）；关闭后任何请求统一返回 eInvalid"
        "，日志宏随之静默——静默段正是演示效果。");

    logAction("导出收班终态拓扑（摘要）");
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
    logVerify("6 个服务 = 5 个内置 + 1 个运动工位：留痕工位已在场景 7 下线，拓扑实时反映运行期服务表");

    YOMK_INFO_TAG("arm.demo", "收班预告：SHUTDOWN 后框架已关闭，此后任何请求统一返回 eInvalid \"YomkServer is not init\"；");
    YOMK_INFO_TAG("arm.demo", "本示例所有日志宏随之静默——下面的静默段正是演示效果，示例日志至此结束，程序停在 getchar 等待回车退出");

    logAction(
        "收班断电：优雅关闭框架", "SHUTDOWN + REQUEST (关闭后守卫)",
        [&] {
            YOMK_SHUTDOWN();
            return YOMK_REQUEST("/MotionService/echo", YomkMkPtr(String, "after shutdown"));
        });
}

int main(int argc, char* argv[])
{
    // 初始化框架（/YomkServerInfo 等 5 个内置服务随之自动启动）
    YOMK_INIT();

    YOMK_INFO_TAG("arm.demo", "机械臂控制 x YomkService 示例：控制柜一次上电会话贯穿 8 个场景，覆盖服务定义与调用全部用户接口");
    YOMK_INFO_TAG(
        "arm.demo",
        "日志按 tag 分层：arm.demo=叙事与结果 / arm.boot=启动编排 / arm.motion=运动服务函数体 / "
        "arm.telemetry=留痕服务函数体（同步调用在调用方线程执行）");

    showArmPowerOnRegister();
    showArmLinkCheck();
    showArmAsyncTeach();
    showArmMoveChain();
    showArmIntrospect();
    showArmStationUpgrade();
    showArmStationOffline();
    showArmPowerOff();

    // 场景 8 已 SHUTDOWN：日志宏全部静默，结束横幅不再可见——
    // 结束预告已并入场景 8 的收班预告锚点，程序在此等待回车退出
    getchar();

    return 0;
}
