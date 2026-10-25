#!/usr/bin/env python3
"""
ExampleYomkFunctionPool —— YomkFunctionPool 动态函数池示例：机械臂产线背景的 6 个场景小示例（面向初学者）

函数池是一个跨模块共享的扁平函数中心：把符合 YomkResponse(pkg) 签名的
函数按名字注册进池子，之后在任意位置按名字调用——机械臂系统中主控、示教器、
视觉工位正是这样共享无状态工具函数（负载合计、班次握手、位姿换算），彼此
无需感知对方实现。同名重复注册即运行时热替换，无需先注销。

本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部 6 个 function_pool_* 用户接口（register 含 2/3/4 参形式）无一遗漏。

日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.tool 工具函数体），
日志流即执行序、顺着读就能明白：每个操作先打"开始执行 xxx"——操作是用户视角
的业务动作，随后结果行标注本次接口调用；CALL 在调用方线程同步执行被调函数，
工具函数体的 arm.tool 日志被夹在开始行与结果行之间，归属一目了然。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_tool_register  [function_pool_register(2参) / function_pool_call]
       开班建工具柜：登记"负载合计"工具后按名调用，入参流进函数、结果流回
       调用方（msg 与 data 双份）——跨模块共享的最小闭环。
场景 2 show_arm_tool_meta      [function_pool_register(4参) / function_pool_info_name / function_pool_call]
       带元数据登记班次握手工具：msg_name 与说明仅作内省元数据，调用时框架
       不做类型校验；INFO_NAME 单查逐字段确认。
场景 3 show_arm_tool_null_guard [function_pool_call(None)]
       空参守卫：CALL 传 None，函数入口判空守卫自动返回 eNo——统一契约，
       arm.tool 无日志自证函数体未执行。
场景 4 show_arm_tool_hot_swap   [function_pool_register(3参) / function_pool_info_name / function_pool_call]
       班中工具升级：同名再注册即热替换（无需注销），CALL 走新实现；msg_name
       与说明元数据随新注册整体刷新（未传说明则清空）。
场景 5 show_arm_tool_dashboard [function_pool_info_names / function_pool_info_name / function_pool_info_all]
       运维面板：函数名清单（字典序）、单查未注册名字演示 eNo not-found、
       全量逐字段导出——框架只回结构化数据，展示格式由用户定义。
场景 6 show_arm_tool_cleanup   [function_pool_unregister / function_pool_call / function_pool_info_all]
       收班清柜：注销后调用、重复注销均返回 eNo（not-found 惯例）；
       INFO_ALL 确认清单已空，班次闭环。

工具函数规划（跨场景流转，模拟多模块共享同一批无状态工具）：
  tool.payloadSum  2 参注册        三轴负载合计（入参各轴 kg 列表，出参合计）
  tool.handshake   4 参注册        班次握手（入参操作工名，出参问候语）
  tool.handshake   3 参再注册(V2)  热替换为升级版问候（场景 4）

运行方式：PYTHONPATH=src python3 examples/ExampleYomkFunctionPool.py
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


# API 结果行：打印 YomkResponse 三要素中的 status/msg。
# CALL 的返回值即被调函数的返回值——结果数据若为 str 则随行展示（如负载合计值）；
# 其余内省数据（list/FuncInfo/FuncInfoList）形态各异，类型不匹配时静默不展示。
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
# 结果行标注本次接口调用；CALL 在调用方线程同步执行被调函数，工具函数体的
# arm.tool 日志被夹在开始行与结果行之间，归属一目了然，读者顺着日志读即可理解
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（返回值需要定制展开的动作，如 INFO_NAMES/INFO_NAME/INFO_ALL）：
# 由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# 自省结果展开：单个函数 FuncInfo 的全部字段逐行展示（函数名行 + 字段行），
# INFO_NAME 与 INFO_ALL 共用；紧随其结果行输出，函数名行以 "-" 起头、字段行缩进对齐
def log_func_info(api, info):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {info.func_name}")
    # msg_name/description 为空表示注册时未指定，防御展示
    msg_text = info.msg_name if info.msg_name else "(未指定)"
    desc_text = info.description if info.description else "(未指定)"
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     msgName: {msg_text}")
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     description: {desc_text}")


# INFO_NAMES 清单行：函数名列表逐行展示（" - " 无序列表风格）
def log_func_line(api, func_name):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {func_name}")


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# ---------------------------------------------------------------------------
# 可注册进函数池的工具函数：签名统一为 YomkResponse(pkg)，全部无状态。
# CALL 在调用方线程同步执行，函数体的 arm.tool 日志会先于调用方结果行出现。
# ---------------------------------------------------------------------------

# 负载合计工具：跨模块共享的无状态计算函数（场景 1 以 2 参形式登记）。
# 入参为各轴负载列表（kg），合计结果同时放进 msg 与 data——调用方两者都能拿到
def payload_sum(pkg) -> YomkApi.YomkResponse:
    # 入参守卫：pkg 为 None 或不是字符串列表时自动返回 eNo
    # （C++ 侧为解包宏判空的统一契约，python 侧为等价的入口守卫，见场景 3）
    if not isinstance(pkg, list) or not all(isinstance(load, str) for load in pkg):
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eNo, " pkg is null or pkg is not StringArray. ")

    YomkApi.console_log_info_tag(
        "arm.tool", f"负载合计工具执行：收到 {len(pkg)} 轴负载（调用方线程同步执行）")

    total_kg = sum(int(load) for load in pkg)

    YomkApi.console_log_info_tag("arm.tool", f"负载合计工具完成：payload={total_kg}kg")

    # msg 描述结果，data 携带结果数据——调用方两者都能拿到
    return YomkApi.YomkResponse(
        YomkApi.ResStatus.eOk, f"payload={total_kg}kg", str(total_kg))


# 班次握手工具：演示带元数据的登记与调用（场景 2 以 4 参形式登记，场景 4 被热替换）
def handshake(pkg) -> YomkApi.YomkResponse:
    if not isinstance(pkg, str):
        return YomkApi.YomkResponse(
            YomkApi.ResStatus.eNo, " pkg is null or pkg is not String. ")
    YomkApi.console_log_info_tag("arm.tool", f"班次握手工具执行：向 {pkg} 回执问候")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "shift hello, " + pkg)


# ---------------------------------------------------------------------------
# 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
# 工具流转：payload_sum/handshake 场景 1/2 登记，handshake 场景 4 热替换，场景 6 清柜。
# ---------------------------------------------------------------------------

# 场景 1：开班建工具柜，主控模块登记负载合计工具并完成首次调用
def show_arm_tool_register():
    log_scene(
        1,
        "开班登记工具函数",
        "function_pool_register(2参) / function_pool_call",
        "开班时主控模块把负载合计工具按名登记进函数池：REGISTER 只登记不执行，"
        "CALL 才按名调用——入参流进函数，返回值原样流回调用方，跨模块共享"
        "无需感知对方实现。")

    log_action("登记负载合计工具 tool.payloadSum", "REGISTER tool.payloadSum",
               lambda: YomkApi.function_pool_register("tool.payloadSum", payload_sum))

    log_action("调用负载合计工具 tool.payloadSum", "CALL tool.payloadSum",
               lambda: YomkApi.function_pool_call("tool.payloadSum", ["12", "8", "5"]))
    log_verify("arm.tool 日志夹在开始行与结果行之间——CALL 在调用方线程同步执行；"
               "返回值即函数返回值，msg 与 data 双份携带 payload=25kg")


# 场景 2：带元数据登记（4 参：msg_name + 说明）与单查确认
def show_arm_tool_meta():
    log_scene(
        2,
        "带元数据登记工具",
        "function_pool_register(4参) / function_pool_info_name / function_pool_call",
        "班次握手工具登记时声明期望消息类型与用途说明：msg_name 与说明仅作内省"
        "元数据，调用时框架不做类型校验；INFO_NAME 单查可逐字段读取元信息。")

    log_action("登记班次握手工具 tool.handshake（带元数据）", "REGISTER tool.handshake (4参)",
               lambda: YomkApi.function_pool_register(
                   "tool.handshake", handshake, "String", "班次握手：向指定操作工回执问候"))

    log_action_start("查看握手工具元信息")
    resp = YomkApi.function_pool_info_name("tool.handshake")
    log_resp("INFO_NAME tool.handshake", resp)
    if resp.data is not None:
        log_func_info("INFO_NAME", resp.data)
    log_verify("msgName=String、description 随行：元数据仅供内省读阅，CALL 时框架不做类型校验")

    log_action("调用班次握手工具 tool.handshake", "CALL tool.handshake",
               lambda: YomkApi.function_pool_call("tool.handshake", "张工"))


# 场景 3：空参调用统一守卫
def show_arm_tool_null_guard():
    log_scene(
        3,
        "空参调用统一守卫",
        "function_pool_call(None)",
        "误传空参调用握手工具：CALL 的入参可为 None，被调函数入口的判空守卫"
        "自动返回 eNo——空参守卫是框架统一契约，调用方无需预处理。")

    log_action("空参调用握手工具 tool.handshake", "CALL tool.handshake (None)",
               lambda: YomkApi.function_pool_call("tool.handshake", None))
    log_verify("开始行与结果行之间无 arm.tool 日志：函数体未执行——eNo 来自函数入口判空守卫，统一契约")


# 场景 4：同名热替换（班中工具升级）
def show_arm_tool_hot_swap():
    log_scene(
        4,
        "工具函数热升级",
        "function_pool_register(3参) / function_pool_info_name / function_pool_call",
        "班中问候口径升级：对已注册名字再次 REGISTER 即热替换函数本体，无需先"
        "注销；msg_name 与说明元数据随新注册整体刷新，随后调用走新实现。")

    def handshake_v2(pkg) -> YomkApi.YomkResponse:
        if not isinstance(pkg, str):
            return YomkApi.YomkResponse(
                YomkApi.ResStatus.eNo, " pkg is null or pkg is not String. ")
        YomkApi.console_log_info_tag("arm.tool", f"班次握手工具 V2 执行：向 {pkg} 升级问候")
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "shift hello V2, " + pkg)

    log_action("热替换班次握手工具 tool.handshake（V2）", "REGISTER tool.handshake (V2, 3参)",
               lambda: YomkApi.function_pool_register("tool.handshake", handshake_v2, "String"))
    log_verify("already exist 提示 + eOk：同名重复注册即运行时热替换，无需先注销")

    log_action_start("复查握手工具元信息")
    resp = YomkApi.function_pool_info_name("tool.handshake")
    log_resp("INFO_NAME tool.handshake", resp)
    if resp.data is not None:
        log_func_info("INFO_NAME", resp.data)
    log_verify("msgName 保持 String、说明未传被清空为(未指定)：元数据随新注册整体替换而非合并")

    log_action("调用升级后的握手工具 tool.handshake", "CALL tool.handshake (V2)",
               lambda: YomkApi.function_pool_call("tool.handshake", "张工"))
    log_verify("arm.tool 日志为 V2 文案：热替换立即生效，旧实现不再被调用")


# 场景 5：产线运维面板巡检
def show_arm_tool_dashboard():
    log_scene(
        5,
        "工具柜运维面板",
        "function_pool_info_names / function_pool_info_name / function_pool_info_all",
        "运维面板巡检：INFO_NAMES 拉取函数名清单；INFO_NAME 单查工具元信息"
        "（未注册名字演示 eNo）；INFO_ALL 全量导出——框架只回结构化数据，"
        "展示格式由用户定义。")

    log_action_start("巡检函数名清单")
    names_resp = YomkApi.function_pool_info_names()
    log_resp("INFO_NAMES", names_resp)
    if names_resp.data:
        for func_name in names_resp.data:
            log_func_line("INFO_NAMES", func_name)
    log_verify("tool.handshake 与 tool.payloadSum 在列：清单按函数名字典序排列")

    log_action_start("单查未注册工具 tool.ghost")
    resp = YomkApi.function_pool_info_name("tool.ghost")
    log_resp("INFO_NAME tool.ghost", resp)
    log_verify("未注册名字 -> eNo(funcName is not register)：not-found 是框架统一惯例")

    log_action_start("全量导出工具元信息")
    resp = YomkApi.function_pool_info_all()
    log_resp("INFO_ALL", resp)
    if resp.data:
        for info in resp.data:
            log_func_info("INFO_ALL", info)


# 场景 6：收班清柜（UNREGISTER 与 not-found 惯例）
def show_arm_tool_cleanup():
    log_scene(
        6,
        "收班清柜注销",
        "function_pool_unregister / function_pool_call / function_pool_info_all",
        "收班下线全部工具：UNREGISTER 把函数移出池子；注销后调用、重复注销均"
        "返回 eNo（not-found 惯例）——eInvalid 仅用于参数无效；INFO_ALL 确认清柜。")

    log_action("注销负载合计工具 tool.payloadSum", "UNREGISTER tool.payloadSum",
               lambda: YomkApi.function_pool_unregister("tool.payloadSum"))

    log_action("调用已注销工具 tool.payloadSum", "CALL tool.payloadSum (已注销)",
               lambda: YomkApi.function_pool_call("tool.payloadSum", None))
    log_verify("已注销名字 -> eNo(funcName is not register)：开始行与结果行之间无 arm.tool 日志，函数已不在池中")

    log_action("重复注销负载合计工具 tool.payloadSum", "UNREGISTER tool.payloadSum (重复)",
               lambda: YomkApi.function_pool_unregister("tool.payloadSum"))
    log_verify("重复注销同样 -> eNo：not-found 是框架统一惯例，不报错也不崩溃")

    log_action("注销班次握手工具 tool.handshake", "UNREGISTER tool.handshake",
               lambda: YomkApi.function_pool_unregister("tool.handshake"))

    log_action_start("巡检清单确认已清空")
    all_resp = YomkApi.function_pool_info_all()
    log_resp("INFO_ALL", all_resp)
    log_verify("函数清单已空：班次结束，工具柜清空，班次闭环"
               if not all_resp.data else "函数清单非空：存在未注销工具")


def main():
    # 初始化框架（函数池服务 /YomkFunctionPool 随之自动启动）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂产线 x YomkFunctionPool 示例：一次班次贯穿 6 个场景，覆盖全部函数池 API")
    YomkApi.console_log_info_tag(
        "arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.tool=工具函数体（调用方线程同步执行）")

    show_arm_tool_register()
    show_arm_tool_meta()
    show_arm_tool_null_guard()
    show_arm_tool_hot_swap()
    show_arm_tool_dashboard()
    show_arm_tool_cleanup()

    YomkApi.console_log_info_tag("arm.demo", "========== 示例结束：按回车退出 ==========")
    input()


if __name__ == "__main__":
    main()
