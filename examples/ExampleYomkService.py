#!/usr/bin/env python3
"""
ExampleYomkService —— YomkService 服务定义与调用示例：机械臂控制背景的 8 个场景小示例（面向初学者）

"一切皆服务"：把业务逻辑封装为 YomkService 子类，注册到框架后任意位置用
URL "/服务名/函数名" 同步或异步调用。框架自动完成路由、删除即停安全（服务
删除/替换后钩子失效不悬垂）与调用拓扑自省——机械臂控制柜里主控、运动控制、
留痕黑匣子正是这样协作的独立工位。

多服务工程结构（组织方式本身也是教程）：
- msgs/ArmMsgs.py             消息包定义（服务间传输的数据结构）
- services/MotionService      运动服务（被 main 直接请求，内部跨服务调留痕）
- services/TelemetryService   留痕服务（被 MotionService 调用，含函数池直调钩子）
- boot/ArmBoot.py             启动编排（before -> start -> after 三阶段）

本示例按"控制柜一次上电会话"贯穿 8 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
服务生命周期与请求类用户接口全部覆盖（自省细节另见 ExampleYomkServerInfo）。

日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.boot 启动编排 /
arm.motion 运动服务函数体 / arm.telemetry 留痕服务函数体），日志流即执行序、
顺着读就能明白：每个操作先打"开始执行 xxx"——操作是用户视角的业务动作，
随后结果行标注本次接口调用；同步 request/function_pool_call 在调用方线程执行
服务函数体，服务日志被夹在开始行与结果行之间，归属一目了然。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_power_on_register [VERSION / SERVER_PTR / NEW_SERVICE / BOOT / INFO_SERVICES]
       控制柜上电与工位注册：框架版本与全局单例自检；NEW_SERVICE 一步创建
       并注册运动服务；BOOT 三阶段编排注册留痕服务（arm.boot 日志夹在开始
       行与结果行之间，before→start→after 顺序可见）；INFO_SERVICES 复查
       在册清单（2 个业务工位排最前 + 5 个内置服务）。
场景 2 show_arm_link_check      [REQUEST]
       通讯自检与寻址异常：REQUEST 同步回环拿到回包 data；请求不存在的
       服务/函数分别得到 service/function not found——eNo 是普通返回值
       不崩溃。
场景 3 show_arm_async_teach     [ASYNC_REQUEST]
       异步下发示教任务：发送即返回不阻塞控制回路，回调稍后在框架异步
       线程池执行——主线程与池线程名对比自证。
场景 4 show_arm_move_chain      [REQUEST / 成员 request() / 强类型回包]
       运动指令调用链：一次 REQUEST 触发 main→/move→/record 调用链
       （arm.motion/arm.telemetry 日志夹在中间），回包 data 携带强类型
       MoveResp——data 不止能传字符串。
场景 5 show_arm_introspect     [INFO_FUNCTIONS / INFO_FUNCTION]
       自省读取工位职能：4 参注册的 /move 带 MoveCmd 元数据与函数说明、
       2 参注册的 /echo 双空；INFO_FUNCTION 按 URL 调阅单张函数名片
       （内省细节详见 ExampleYomkServerInfo）。
场景 6 show_arm_station_upgrade [FUNCTIONPOOL_CALL / ADD_SERVICE]
       留痕工位升级：同名 ADD_SERVICE 即替换——替换前后直调钩子
       telemetry_hook（diag check V1→V2），[实例#N] 日志自证服务与
       钩子一并换新。
场景 7 show_arm_station_offline [DEL_SERVICE / FUNCTIONPOOL_CALL / REQUEST]
       留痕工位下线：DEL_SERVICE 删除即停——deinit 显式注销钩子立即失效
       （eNo 不悬垂）、move 因留痕缺失失败沿调用链传播；重复注销返回 -1。
场景 8 show_arm_power_off       [INFO_ALL / SHUTDOWN]
       收班断电：INFO_ALL 看终态拓扑；预告后 SHUTDOWN 排空在途请求并
       释放全部服务，关闭后任何请求统一返回 eInvalid——日志门面随之静默，
       静默段正是演示效果，示例日志到此为止。

涉及的门面 API（YomkApi）：
  YomkApi.init / shutdown / __version__        框架生命周期
  new_service(cls)                             创建并注册服务（最常用）
  add_service(srv, name)                       注册已有实例（同名即替换）
  del_service(name)                            注销服务（删除即停）
  boot(boot)                                   启动编排（before -> start -> after）
  request / async_request                      同步 / 异步请求
  server() / server_instance()                 全局单例
  server_info_services/functions/function/all  调用拓扑自省
  install_func(2/4 参) / 成员 request          服务侧 API

运行方式：PYTHONPATH=src python3 examples/ExampleYomkService.py
"""

import threading
import time

import YomkApi

from boot.ArmBoot import ArmBoot
from msgs.ArmMsgs import MoveCmd, MoveResp


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
# 回包挂字符串 data 时随行展示；结构化包形态各异，由调用处专项展开
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
# 结果行标注本次接口调用；同步 request/function_pool_call 在调用方线程执行服务函数体，
# arm.motion/arm.telemetry 等服务日志被夹在开始行与结果行之间，归属一目了然
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（返回值需要定制展开的动作，如清单/拓扑类查询与异步请求）：
# 由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# 操作日志（int 返回版）：NEW_SERVICE/ADD_SERVICE/DEL_SERVICE/BOOT 等框架管理
# 接口返回 int（0=成功，-1=失败），先打开始行再调用，结果行标注返回值
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


# 拓扑摘要行：服务名 + 函数计数（收班终态用，拓扑全量展开详见 ExampleYomkServerInfo）
def log_srv_brief(api, srv_name, func_count):
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}] - {srv_name}（{func_count} 个功能函数）")


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# ---------------------------------------------------------------------------
# 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按上电会话顺序编排。
# 工位流转：/MotionService 场景 1 注册，场景 2/3/4/5 请求与自省读它；
# /TelemetryService 场景 1 经 BOOT 注册，场景 6 同名替换、场景 7 下线；
# 场景 8 断电。
# ---------------------------------------------------------------------------

# 场景 1：控制柜上电与工位注册（自检 + NEW_SERVICE + BOOT + 在册清单）
def show_arm_power_on_register():
    log_scene(
        1,
        "控制柜上电与工位注册",
        "VERSION / SERVER_PTR / NEW_SERVICE / BOOT / INFO_SERVICES",
        "控制柜上电先自检（框架版本、全局单例就绪），再把业务工位注册进框架："
        "NEW_SERVICE 一步创建并注册运动服务；BOOT 按 before→start→after 三阶段"
        "编排注册留痕服务；INFO_SERVICES 复查在册清单。")

    log_action_start("控制柜上电自检：读取框架版本与全局单例")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[VERSION] version={YomkApi.__version__}")
    YomkApi.console_log_info_tag(
        "arm.demo",
        f"[SERVER_PTR] instance={'ready' if YomkApi.server_instance() is not None else 'null'}")
    log_verify("init 后单例就绪：server_instance() 可判空，server() 供服务构造时取 server 引用")

    log_action_int(
        "运动工位上线：一步创建并注册 /MotionService", "NEW_SERVICE /MotionService",
        lambda: YomkApi.new_service(MotionService))
    log_verify("返回 0 即注册成功：URL 前缀 /MotionService 生效，init() 已被框架调用（arm.motion 的 init 日志为证）")

    log_action_int(
        "启动编排注册留痕工位 /TelemetryService", "BOOT ArmBoot",
        lambda: YomkApi.boot(ArmBoot(["/TelemetryService"])))
    log_verify("arm.boot 三段日志夹在开始行与结果行之间：boot 按 before→start→after 顺序执行")

    log_action_start("复查控制柜在册服务清单")
    names_resp = YomkApi.server_info_services()
    log_resp("INFO_SERVICES", names_resp)
    if names_resp.data:
        for srv_name in names_resp.data:
            log_string_line("INFO_SERVICES", srv_name)
    log_verify("清单 7 项 = 2 个业务工位（排最前）+ 5 个内置服务，按字典序排列")


# 场景 2：通讯自检与寻址异常（REQUEST 回环 + not-found 分支）
def show_arm_link_check():
    log_scene(
        2,
        "通讯自检与寻址异常",
        "REQUEST",
        "主控与运动工位做通讯自检：REQUEST 同步请求 /MotionService/echo，回包 "
        "data 随行；再故意请求不存在的服务与函数——not-found 是普通返回值，"
        "调用失败不崩溃。")

    log_action(
        "通讯自检：请求运动服务回环", "REQUEST /MotionService/echo",
        lambda: YomkApi.request("/MotionService/echo", "ping"))
    log_verify("arm.motion 日志夹在开始行与结果行之间——request 在调用方线程同步执行；回包 data 携带回显文本")

    log_action(
        "请求不存在的服务 /GhostService", "REQUEST /GhostService/echo",
        lambda: YomkApi.request("/GhostService/echo", "ping"))

    log_action(
        "请求运动服务不存在的函数", "REQUEST /MotionService/not_exist",
        lambda: YomkApi.request("/MotionService/not_exist", "ping"))
    log_verify("两种 not-found 均为 eNo 且 msg 说明原因：寻址失败是普通返回值，不报错不崩溃")


# 场景 3：异步下发示教任务（ASYNC_REQUEST 线程自证）
def show_arm_async_teach():
    log_scene(
        3,
        "异步下发示教任务",
        "ASYNC_REQUEST",
        "示教任务走异步通道：ASYNC_REQUEST 发送即返回、不阻塞控制回路，回调"
        "稍后在框架异步线程池执行——主线程与池线程名对比自证。")

    YomkApi.console_log_info_tag(
        "arm.demo", f"主线程: {threading.current_thread().name}")

    def on_teach_done(resp):
        YomkApi.console_log_info_tag(
            "arm.demo",
            f'[ASYNC_REQUEST 回调] status={resp.status.value}, msg="{resp.msg}"'
            f"（池线程: {threading.current_thread().name}）")

    log_action_start("异步下发示教点自检请求")
    YomkApi.async_request("/MotionService/echo", "teach point #1", on_teach_done)
    log_verify("ASYNC_REQUEST 无同步结果行：发送即返回，控制回路不等回调")

    # 等待异步回调送达，保证回调日志先于下一条验证行出现、可读
    time.sleep(0.3)
    log_verify("池线程不同于主线程：请求在框架内置线程池并发执行，不阻塞控制回路")


# 场景 4：运动指令调用链（跨服务调用 + 强类型回包）
def show_arm_move_chain():
    log_scene(
        4,
        "运动指令调用链",
        "REQUEST / 成员 request() / 强类型回包",
        "下发运动指令：一次 REQUEST 触发调用链 main→/move→/record——运动服务"
        "内部用成员 request() 跨服务调留痕工位（与全局 API 等价、无需单例）；"
        "到位回执用自定义 MoveResp 强类型回包。")

    move_resp = log_action(
        "下发运动指令：运动到 (100.0, 50.0, 200.0)mm", "REQUEST /MotionService/move",
        lambda: YomkApi.request("/MotionService/move", MoveCmd(100.0, 50.0, 200.0, 0.8)))

    log_action_start("展开到位回执 MoveResp")
    data = move_resp.data
    if isinstance(data, MoveResp):
        YomkApi.console_log_info_tag(
            "arm.demo",
            f"[REQUEST /MotionService/move] | MoveResp 到位 ({data.reach_x}, {data.reach_y}, {data.reach_z})mm")
    else:
        YomkApi.console_log_info_tag("arm.demo", "[REQUEST /MotionService/move] | (no MoveResp data)")
    log_verify("arm.motion 与 arm.telemetry 日志夹在开始行与结果行之间：调用链 main→/move→/record 同步执行；data 携带强类型 MoveResp")


# 场景 5：自省读取工位职能（简要展示，细节见 ExampleYomkServerInfo）
def show_arm_introspect():
    log_scene(
        5,
        "自省读取工位职能",
        "INFO_FUNCTIONS / INFO_FUNCTION",
        "运维面板读取运动工位职能：INFO_FUNCTIONS 拉取函数清单（FuncInfo "
        "结构化），INFO_FUNCTION 按 URL 调阅单张函数名片——4 参注册的 /move "
        "带元数据与说明，2 参注册的 /echo 双空（内省细节详见 "
        "ExampleYomkServerInfo）。")

    log_action_start("查阅运动工位职能清单")
    funcs_resp = YomkApi.server_info_functions("/MotionService")
    log_resp("INFO_FUNCTIONS /MotionService", funcs_resp)
    if funcs_resp.data:
        for func in funcs_resp.data:
            log_func_info("INFO_FUNCTIONS", func)
    log_verify("msgName 即 install_func 第三参声明的期望消息类型、description 即第四参函数说明；2 参注册的 /echo 两字段为(未指定)")

    log_action_start("调阅运动指令函数名片")
    resp = YomkApi.server_info_function("/MotionService/move")
    log_resp("INFO_FUNCTION /MotionService/move", resp)
    if resp.data is not None:
        log_func_info("INFO_FUNCTION", resp.data)
    log_verify("命中契约：msg 恒 ok、元数据走 data")


# 场景 6：留痕工位升级（同名替换，服务与钩子一并热切换）
def show_arm_station_upgrade():
    log_scene(
        6,
        "留痕工位升级",
        "FUNCTIONPOOL_CALL / ADD_SERVICE",
        "班中留痕工位升级换新：同名 ADD_SERVICE 即替换，旧实例下线、新实例"
        "接管并重挂同名钩子——替换前后直调钩子（diag check V1→V2）自证服务"
        "与钩子一并替换。")

    log_action(
        "直调留痕钩子（替换前）", "FUNCTIONPOOL_CALL telemetry_hook (V1)",
        lambda: YomkApi.function_pool_call("telemetry_hook", "diag check V1"))
    log_verify("留痕记录 [实例#1]：diag check V1——钩子由当前实例 init 注册、按名直调")

    log_action_int(
        "留痕工位升级：同名注册替换 /TelemetryService", "ADD_SERVICE /TelemetryService",
        lambda: YomkApi.add_service(TelemetryService(YomkApi.server()), "/TelemetryService"))
    log_verify("框架直出 service already exists -> update to current service：旧实例 deinit 注销钩子，实例 #1 随即回收")
    log_verify("新实例 init 重挂 telemetry_hook，函数池直出 already exist -> update to current function：服务与钩子一并换新")

    log_action(
        "直调留痕钩子（替换后）", "FUNCTIONPOOL_CALL telemetry_hook (V2)",
        lambda: YomkApi.function_pool_call("telemetry_hook", "diag check V2"))
    log_verify("留痕记录 [实例#2]：diag check V2——旧实例已回收而钩子仍可用，钩子确已换新")


# 场景 7：留痕工位下线（删除即停与断链传播）
def show_arm_station_offline():
    log_scene(
        7,
        "留痕工位下线",
        "DEL_SERVICE / FUNCTIONPOOL_CALL / REQUEST",
        "班末留痕工位下线：DEL_SERVICE 删除即停——deinit 显式注销钩子立即"
        "失效，运动调用链因留痕缺失断链传播；注销失败是普通返回值。")

    log_action_int(
        "留痕工位下线：注销 /TelemetryService", "DEL_SERVICE /TelemetryService",
        lambda: YomkApi.del_service("/TelemetryService"))

    log_action_int(
        "重复注销已下线工位", "DEL_SERVICE /TelemetryService (重复)",
        lambda: YomkApi.del_service("/TelemetryService"))
    log_verify("服务已不存在 -> ret=-1：注销失败是普通返回值，不报错不崩溃")

    log_action(
        "直调留痕钩子（已下线）", "FUNCTIONPOOL_CALL telemetry_hook (已下线)",
        lambda: YomkApi.function_pool_call("telemetry_hook", "diag check V2"))
    log_verify("钩子返回 eNo(funcName is not register)：deinit 显式注销承担失效语义，删除即停不悬垂")

    log_action(
        "重发运动指令（留痕缺失）", "REQUEST /MotionService/move",
        lambda: YomkApi.request("/MotionService/move", MoveCmd(100.0, 50.0, 200.0, 0.8)))
    log_verify("move 返回 eNo(move failed: ...)：留痕是运动的依赖前置，失败沿调用链传播")


# 场景 8：收班断电（INFO_ALL 终态 + SHUTDOWN 后守卫）
def show_arm_power_off():
    log_scene(
        8,
        "收班断电",
        "INFO_ALL / SHUTDOWN",
        "收班先看终态拓扑，再断电：SHUTDOWN 先停异步请求池（拒新→排空→join），"
        "再逐个 deinit 并释放全部服务（幂等）；关闭后任何请求统一返回 eInvalid"
        "，日志门面随之静默——静默段正是演示效果。")

    log_action_start("导出收班终态拓扑（摘要）")
    all_resp = YomkApi.server_info_all()
    log_resp("INFO_ALL", all_resp)
    if all_resp.data:
        for srv in all_resp.data:
            log_srv_brief("INFO_ALL", srv.service_name, len(srv.funcs))
    log_verify("6 个服务 = 5 个内置 + 1 个运动工位：留痕工位已在场景 7 下线，拓扑实时反映运行期服务表")

    YomkApi.console_log_info_tag(
        "arm.demo", '收班预告：shutdown 后框架已关闭，此后任何请求统一返回 eInvalid "YomkServer is not init"；')
    YomkApi.console_log_info_tag(
        "arm.demo", "本示例所有日志门面随之静默——下面的静默段正是演示效果，示例日志至此结束，程序停在 input 等待回车退出")

    def shutdown_and_guard():
        YomkApi.shutdown()
        return YomkApi.request("/MotionService/echo", "after shutdown")  # eInvalid：结果行随日志门面静默，行为已由收班预告说明

    log_action("收班断电：优雅关闭框架", "SHUTDOWN + REQUEST (关闭后守卫)", shutdown_and_guard)


def main():
    # 初始化框架（/YomkServerInfo 等 5 个内置服务随之自动启动）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂控制 x YomkService 示例：控制柜一次上电会话贯穿 8 个场景，覆盖服务定义与调用全部用户接口")
    YomkApi.console_log_info_tag(
        "arm.demo",
        "日志按 tag 分层：arm.demo=叙事与结果 / arm.boot=启动编排 / arm.motion=运动服务函数体 / "
        "arm.telemetry=留痕服务函数体（同步调用在调用方线程执行）")

    show_arm_power_on_register()
    show_arm_link_check()
    show_arm_async_teach()
    show_arm_move_chain()
    show_arm_introspect()
    show_arm_station_upgrade()
    show_arm_station_offline()
    show_arm_power_off()

    # 场景 8 已 shutdown：日志门面全部静默，结束横幅不再可见——
    # 结束预告已并入场景 8 的收班预告锚点，程序在此等待回车退出
    input()


if __name__ == "__main__":
    main()
