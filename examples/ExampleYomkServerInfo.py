#!/usr/bin/env python3
"""
ExampleYomkServerInfo —— YomkServerInfo 服务自省示例：机械臂产线背景的 6 个场景小示例（面向初学者）

服务自省即"运行期问框架"：/YomkServerInfo 是随 YomkApi.init 自动启动的内置服务，
提供服务清单、指定服务的功能函数清单、单函数元信息与全量拓扑四类查询——
调试与运维时用它确认"产线上现在有哪些工位（服务）、每个功能函数期望什么
消息类型"，无需翻源码即可掌握调用拓扑。

本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部 4 个自省门面 API（server_info_services/functions/function/all）无一遗漏
（服务注册/请求/注销/关闭作为自省对象的素材接口随之演示）。

日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.station
装配工位函数体），日志流即执行序、顺着读就能明白：每个操作先打"开始执行
xxx"——操作是用户视角的业务动作，随后结果行标注本次接口调用；request
在调用方线程同步执行工位函数，arm.station 日志被夹在开始行与结果行之间，
归属一目了然。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_plant_roster     [INFO_SERVICES / NEW_SERVICE / REQUEST]
       开班点名与工位上线：内置服务清单打底，NEW_SERVICE 注册装配工位后
       清单立即多一项；REQUEST 试运行 /report 与 /quality_check 证明注册
       即可调用（工位函数体 arm.station 日志夹在开始行与结果行之间）。
场景 2 show_arm_station_functions [INFO_FUNCTIONS]
       工位职能清单：FuncInfo 结构化清单（4 参注册的 /quality_check
       带 QualityCheckReq 元数据与函数说明，2 参注册的 /report 双空）；
       不存在的工位返回空清单 eOk，空服务名返回 eInvalid。
场景 3 show_arm_func_lookup      [INFO_FUNCTION]
       函数名片单查：命中返回结构化 FuncInfo 包（msg 恒 "ok"，
       msg_name=期望消息类型名、description=函数说明）；4 种坏 URL 均
       返回 eNo（url 解析失败 / 目标不存在），是普通返回值不崩溃。
场景 4 show_arm_plant_topology   [INFO_ALL]
       全厂拓扑总览：层级列表逐服务逐函数导出——排障时一次看清
       "哪些工位在册、各自能干什么"。
场景 5 show_arm_live_view        [INFO_SERVICES / INFO_ALL / DEL_SERVICE]
       工位下线实时视图：DEL_SERVICE 后清单与拓扑立即少一项（无需重启），
       注销不存在的服务返回 -1——自省读的是运行期服务表。
场景 6 show_arm_power_off        [SHUTDOWN / INFO_SERVICES]
       收班断电：SHUTDOWN 排空在途请求并释放全部服务，关闭后任何自省调用
       统一返回 eInvalid "YomkServer is not init"；日志门面随之静默——
       静默即演示效果，收班预告已提前说明，示例日志到此为止。

演示服务规划（机械臂产线装配工位，跨场景流转）：
  /ArmStation            NEW_SERVICE 注册（场景 1）、DEL_SERVICE 下线（场景 5）
  /report        2 参注册  工位状态上报（入参 String 状态文本，回执确认；无元数据）
  /quality_check 4 参注册  质量抽检（入参 QualityCheckReq；声明期望消息类型
                          与函数说明，供场景 2/3 的自省识别）

运行方式：PYTHONPATH=src python3 examples/ExampleYomkServerInfo.py
"""

import YomkApi


# ---------------------------------------------------------------------------
# 辅助输出：全部走 YomkLogger 控制台日志（console_log_info_tag 等），无 print。
# 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
# ---------------------------------------------------------------------------

# 场景横幅：编号 + 标题 + 涉及接口，随后一行场景背景；横幅前留一个空行分隔场景
def log_scene(n, title, apis, explain):
    YomkApi.console_log_info_tag("arm.demo", "")
    YomkApi.console_log_info_tag(
        "arm.demo", f"========== 场景 {n}：{title}（{apis}）==========")
    YomkApi.console_log_info_tag("arm.demo", "场景背景：" + explain)


# API 结果行：打印 YomkResponse 三要素中的 status/msg（数据形态各异，由调用处按需展开）。
# 字符串 data（如 REQUEST 回执）随行展示；结构化清单/拓扑形态各异，由调用处专项展开
# status 三态：0=eOk 成功；1=eNo 名字不存在或被拒绝；-1=eInvalid 参数无效或未初始化
def log_resp(api, resp):
    if resp.status == YomkApi.ResStatus.eOk:
        note = "(eOk 成功)"
    elif resp.status == YomkApi.ResStatus.eNo:
        note = "(eNo 拒绝或不存在)"
    else:
        note = "(eInvalid 未初始化或参数无效)"
    line = f'[{api}] status={resp.status.value}{note}, msg="{resp.msg}"'
    if resp.status == YomkApi.ResStatus.eOk and isinstance(resp.data, str):
        line += f', data="{resp.data}"'
    YomkApi.console_log_info_tag("arm.demo", line)


# 操作日志（YomkResponse 版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
# 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
# 结果行标注本次接口调用；request 在调用方线程同步执行工位函数，arm.station
# 日志被夹在开始行与结果行之间，归属一目了然，读者顺着日志读即可理解
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（返回值需要定制展开的动作，如清单/拓扑/名片类查询）：
# 由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# 操作日志（int 返回版）：NEW_SERVICE/DEL_SERVICE 等框架管理接口返回 int
# （0=成功，-1=失败），先打开始行再调用，结果行标注返回值
def log_action_int(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    ret = call()
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}] ret={ret}" + ("(成功)" if ret == 0 else "(失败)"))


# 服务清单行：清单逐行展示（" - " 无序列表风格）
def log_string_line(api, line):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {line}")


# 自省结果展开：单个函数 FuncInfo 的全部字段逐行展示（函数名行 + 字段行），
# FUNCTIONS 清单与 FUNCTION 单查共用；紧随其结果行输出，函数名行以 "-" 起头、
# 字段行缩进对齐，元数据为空时兜底"(未指定)"
def log_func_info(api, info):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {info.func_name}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     msgName: {info.msg_name if info.msg_name else '(未指定)'}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     description: {info.description if info.description else '(未指定)'}")


# 拓扑展开：服务名行 + 该服务的函数清单行（函数名 + 可选 MsgName 标注），
# INFO_ALL 层级包逐服务逐函数导出（框架只回结构化数据，展示格式由用户定义）
def log_srv_line(api, srv_name):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {srv_name}")


def log_topology_func(api, info):
    # 二级函数行退两级空格：相对服务名行（"- " 前缀，内容起始差 2 字符）再深一层，
    # 层级差 4 字符在长日志前缀下依然分明
    line = "      " + info.func_name
    if info.msg_name:
        line += " [" + info.msg_name + "]"
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]{line}")


def log_topology(api, resp):
    if resp.data is None:
        return
    for srv in resp.data:
        log_srv_line(api, srv.service_name)
        for func in srv.funcs:
            log_topology_func(api, func)


# 拓扑摘要行：服务名 + 函数计数（工位下线后复查用，避免重复全量展开）
def log_srv_brief(api, srv_name, func_count):
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}] - {srv_name}（{func_count} 个功能函数）")


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# ---------------------------------------------------------------------------
# 自省素材：机械臂产线装配工位服务。2 参注册的 /report 无元数据，4 参注册的
# /quality_check 声明期望消息类型 QualityCheckReq 并附函数说明——差异将由
# 场景 2/3 的自省结果直接呈现。request 在调用方线程同步执行函数体，
# arm.station 日志夹在调用方开始行与结果行之间，归属一目了然。
# ---------------------------------------------------------------------------

class QualityCheckReq:
    # 质量抽检请求：此处仅作 /quality_check 的期望消息类型元数据，供自省识别
    def __init__(self, station="", count=0):
        self.station = station  # 工位号
        self.count = count      # 抽检件数


class ArmStationService(YomkApi.YomkService):
    def __init__(self, server):
        super().__init__(server)
        # 服务名：URL 前缀，必须 / 开头且全局唯一
        self.set_name("/ArmStation")

    def init(self):
        # 2 参注册：无元数据；4 参注册：声明 /quality_check 期望 QualityCheckReq
        # 并附函数说明，供自省识别
        self.install_func("/report", self.report)
        self.install_func(
            "/quality_check", self.quality_check, "QualityCheckReq",
            "装配工位质量抽检：按件抽检并回执结论")
        YomkApi.console_log_info_tag(
            "arm.station", "init: install /report (no meta), /quality_check [QualityCheckReq] + description")
        return 0

    # 工位状态上报：收到状态文本回执确认
    def report(self, pkg) -> YomkApi.YomkResponse:
        if not isinstance(pkg, str):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not String. ")
        YomkApi.console_log_info_tag("arm.station", f'工位状态上报：收到 "{pkg}"')
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eOk, "station report received: " + pkg)

    # 质量抽检：按件抽检并回执结论
    def quality_check(self, pkg) -> YomkApi.YomkResponse:
        if not isinstance(pkg, QualityCheckReq):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not QualityCheckReq. ")
        YomkApi.console_log_info_tag(
            "arm.station", f"质量抽检执行：工位 {pkg.station} 抽检 {pkg.count} 件")
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eOk,
            f"quality check passed: {pkg.station}, {pkg.count} pcs")


# ---------------------------------------------------------------------------
# 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
# 工位流转：/ArmStation 场景 1 上线，场景 2/3/4 自省读它，场景 5 下线，场景 6 断电。
# ---------------------------------------------------------------------------

# 场景 1：开班点名与工位上线（注册 + 试运行）
def show_arm_plant_roster():
    log_scene(
        1,
        "开班点名与工位上线",
        "INFO_SERVICES / NEW_SERVICE / REQUEST",
        "开班点名看产线在册工位：INFO_SERVICES 查到的就是框架运行期服务表；"
        "NEW_SERVICE 把装配工位注册进产线，REQUEST 试运行两个功能函数证明"
        "注册即可调用。")

    log_action_start("开班点名：查询产线在册服务")
    names_resp = YomkApi.server_info_services()
    log_resp("INFO_SERVICES", names_resp)
    if names_resp.data:
        for srv_name in names_resp.data:
            log_string_line("INFO_SERVICES", srv_name)
    log_verify("5 项即内置服务（/YomkContext /YomkEventLoop /YomkFunctionPool /YomkLogger /YomkServerInfo），自省服务开箱即用无需注册")

    log_action_int("装配工位上线：注册 /ArmStation", "NEW_SERVICE /ArmStation",
                   lambda: YomkApi.new_service(ArmStationService))

    log_action_start("复查在册服务清单")
    names_resp = YomkApi.server_info_services()
    log_resp("INFO_SERVICES", names_resp)
    if names_resp.data:
        for srv_name in names_resp.data:
            log_string_line("INFO_SERVICES", srv_name)
    log_verify("清单多一项 /ArmStation 且排最前：服务名按字典序排列，注册即时反映到下一次查询")

    log_action("试运行工位功能：状态上报", "REQUEST /ArmStation/report",
               lambda: YomkApi.request("/ArmStation/report", "joint ready"))
    log_action("试运行工位功能：质量抽检", "REQUEST /ArmStation/quality_check",
               lambda: YomkApi.request("/ArmStation/quality_check", QualityCheckReq("S3", 5)))
    log_verify("arm.station 日志夹在开始行与结果行之间——request 在调用方线程同步执行；返回值即函数返回值，msg 携带回执")


# 场景 2：工位职能清单（INFO_FUNCTIONS 三分支）
def show_arm_station_functions():
    log_scene(
        2,
        "工位职能清单",
        "INFO_FUNCTIONS",
        "查阅装配工位能干什么：INFO_FUNCTIONS 返回 FuncInfo 结构化清单"
        "（func_name/msg_name/description）——4 参注册的 /quality_check 带"
        "元数据，2 参注册的 /report 双空，差异一目了然。")

    log_action_start("查阅装配工位职能清单")
    funcs_resp = YomkApi.server_info_functions("/ArmStation")
    log_resp("INFO_FUNCTIONS /ArmStation", funcs_resp)
    if funcs_resp.data:
        for func in funcs_resp.data:
            log_func_info("INFO_FUNCTIONS", func)
    log_verify("msgName 即 install_func 第三参声明的期望消息类型、description 即第四参函数说明；2 参注册的 /report 两字段为(未指定)")

    log_action_start("查阅不存在工位 /GhostStation 的职能清单")
    funcs_resp = YomkApi.server_info_functions("/GhostStation")
    log_resp("INFO_FUNCTIONS /GhostStation", funcs_resp)
    log_verify("服务不存在 -> 空清单 eOk：查询失败不崩溃，仍是普通返回值")

    log_action_start("以空服务名查阅职能清单")
    funcs_resp = YomkApi.server_info_functions("")
    log_resp("INFO_FUNCTIONS (空服务名)", funcs_resp)
    log_verify("空服务名 -> eInvalid(srvName is empty)：参数无效与目标不存在是两种不同状态")


# 场景 3：功能函数名片单查（INFO_FUNCTION 命中契约 + 4 种坏 URL）
def show_arm_func_lookup():
    log_scene(
        3,
        "功能函数名片单查",
        "INFO_FUNCTION",
        "按 URL 调阅单个函数的名片：命中返回结构化 FuncInfo 包（msg 恒 ok），"
        "msgName 与 description 承载注册时声明的元数据；url 解析失败与目标"
        "不存在均为 eNo。")

    log_action_start("调阅质量抽检函数名片")
    resp = YomkApi.server_info_function("/ArmStation/quality_check")
    log_resp("INFO_FUNCTION /ArmStation/quality_check", resp)
    if resp.data is not None:
        log_func_info("INFO_FUNCTION", resp.data)
    log_verify("命中契约：msg 恒 ok、数据走 data；msgName=QualityCheckReq、description=第四参说明——与场景 2 清单同族")

    log_action_start("调阅状态上报函数名片")
    resp = YomkApi.server_info_function("/ArmStation/report")
    log_resp("INFO_FUNCTION /ArmStation/report", resp)
    if resp.data is not None:
        log_func_info("INFO_FUNCTION", resp.data)
    log_verify("2 参注册的 /report msgName 为(未指定)：元数据是可选声明，不影响调用")

    log_action("以不以 / 开头的 url 单查", "INFO_FUNCTION ArmStation/quality_check (无/)",
               lambda: YomkApi.server_info_function("ArmStation/quality_check"))
    log_action("以缺函数名的 url 单查", "INFO_FUNCTION /ArmStation (缺函数名)",
               lambda: YomkApi.server_info_function("/ArmStation"))
    log_action("以服务不存在的 url 单查", "INFO_FUNCTION /GhostStation/hi (服务不存在)",
               lambda: YomkApi.server_info_function("/GhostStation/hi"))
    log_action("以函数不存在的 url 单查", "INFO_FUNCTION /ArmStation/ghost (函数不存在)",
               lambda: YomkApi.server_info_function("/ArmStation/ghost"))
    log_verify("4 种坏 URL 均为 eNo 且 msg 说明原因：url 解析失败 / 目标不存在是普通返回值，不报错不崩溃")


# 场景 4：全厂拓扑总览（INFO_ALL）
def show_arm_plant_topology():
    log_scene(
        4,
        "全厂拓扑总览",
        "INFO_ALL",
        "一次拉取全厂服务拓扑：INFO_ALL 返回层级列表"
        "（service_name 服务名 + funcs 函数元信息列表），排障时一眼看清"
        "调用拓扑。")

    log_action_start("导出全厂服务拓扑")
    all_resp = YomkApi.server_info_all()
    log_resp("INFO_ALL", all_resp)
    log_topology("INFO_ALL", all_resp)
    log_verify("6 个服务 = 5 个内置 + 1 个装配工位，函数名带 / 前缀、带元数据的附 [MsgName] 标注——层级自上而下逐服务展开")


# 场景 5：工位下线实时视图（DEL_SERVICE + 自省复查）
def show_arm_live_view():
    log_scene(
        5,
        "工位下线实时视图",
        "INFO_SERVICES / INFO_ALL / DEL_SERVICE",
        "班中工位检修下线：DEL_SERVICE 把工位移出服务表，清单与拓扑立即少"
        "一项，无需重启或刷新——自省读的是运行期服务表，注册/删除即时反映"
        "到下一次查询。")

    log_action_int("装配工位下线：注销 /ArmStation", "DEL_SERVICE /ArmStation",
                   lambda: YomkApi.del_service("/ArmStation"))

    log_action_start("复查在册服务清单")
    names_resp = YomkApi.server_info_services()
    log_resp("INFO_SERVICES", names_resp)
    if names_resp.data:
        for srv_name in names_resp.data:
            log_string_line("INFO_SERVICES", srv_name)
    log_verify("清单回到 5 项内置服务：删除即时生效，无需重启")

    log_action_start("复查全厂拓扑终态")
    all_resp = YomkApi.server_info_all()
    log_resp("INFO_ALL", all_resp)
    if all_resp.data:
        for srv in all_resp.data:
            log_srv_brief("INFO_ALL", srv.service_name, len(srv.funcs))
    log_verify("拓扑只剩 5 个内置服务，各服务函数计数如实呈现：工位已从调用拓扑消失")

    log_action_int("注销不存在的工位 /GhostStation", "DEL_SERVICE /GhostStation",
                   lambda: YomkApi.del_service("/GhostStation"))
    log_verify("不存在的服务 -> ret=-1（失败）：注销失败是普通返回值不崩溃（框架侧另有直出提示行）")


# 场景 6：收班断电（SHUTDOWN 后自省守卫）
def show_arm_power_off():
    log_scene(
        6,
        "收班断电",
        "SHUTDOWN / INFO_SERVICES",
        "收班断电前先预告：SHUTDOWN 排空在途请求并释放全部服务（幂等），"
        "关闭后框架单例清空，任何自省调用统一返回 eInvalid——本条守卫演示"
        "将在静默中完成，静默即演示效果。")

    YomkApi.console_log_info_tag(
        "arm.demo", '收班预告：shutdown 后框架已关闭，此后任何自省调用统一返回 eInvalid "YomkServer is not init"；')
    YomkApi.console_log_info_tag(
        "arm.demo", "本示例所有日志门面随之静默——下面的静默段正是演示效果，示例日志至此结束，程序停在 input 等待回车退出")

    def shutdown_and_guard():
        YomkApi.shutdown()
        return YomkApi.server_info_services()  # eInvalid：结果行随日志门面静默，行为已由收班预告说明

    log_action("收班断电：优雅关闭框架", "SHUTDOWN + INFO_SERVICES (关闭后守卫)", shutdown_and_guard)


def main():
    # 初始化框架（/YomkServerInfo 等 5 个内置服务随之自动启动，自省开箱即用）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂产线 x YomkServerInfo 示例：一次班次贯穿 6 个场景，覆盖全部服务自省 API")
    YomkApi.console_log_info_tag(
        "arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.station=装配工位服务函数体（request 调用方线程同步执行）")

    show_arm_plant_roster()
    show_arm_station_functions()
    show_arm_func_lookup()
    show_arm_plant_topology()
    show_arm_live_view()
    show_arm_power_off()

    # 场景 6 已 shutdown：日志门面全部静默，结束横幅不再可见——
    # 结束预告已并入场景 6 的收班预告锚点，程序在此等待回车退出
    input()


if __name__ == "__main__":
    main()
