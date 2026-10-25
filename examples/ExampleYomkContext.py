#!/usr/bin/env python3
"""
ExampleYomkContext —— YomkContext 共享上下文示例：机械臂控制背景的 8 个场景小示例（面向初学者）

上下文是全局共享的强类型键值状态机：把值按 key 存起来，任意位置按 key
读写——机械臂系统中主控、示教器、安全 PLC、数字孪生正是这样共享状态。
在此之上还有两道可选防线：变更前校验门控（Checker，返回 eReject 可拦截非法
写入）与变更后通知（Monitor，写入成功后收到键值快照，同步/异步可选）。

本示例用"机械臂一次作业会话"贯穿 8 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部 14 个 context_* 用户接口无一遗漏。

日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.guard 校验 /
arm.watch 通知 / arm.telemetry 上报），日志流即执行序、顺着读就能明白：
每个操作先打"开始执行 xxx"——操作是用户视角的业务动作（可以是一个接口
调用，也可以是多个接口调用组合成的完整动作），随后结果行标注本次接口
调用；同步 checker/monitor 回调插入的日志被夹在两者之间，归属一目了然；
异步通知则在结果行之后送达。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_boot_create      [context_create]
       控制柜上电，主控模块写入初始状态（模式/速度倍率/伺服状态）；
       重复创建同名键被拒——创建是排他的，更新请用 SET。
场景 2 show_arm_read_status      [context_get]
       示教器读速度倍率命中只读快照；查询未上线负载模块的键返回兜底
       默认值（is 同一对象自证兜底路径，调用方永不拿到空值）。
场景 3 show_arm_update_status    [context_set]
       切自动模式并提速；误用 float 改 str 键被拒（强类型防误写）；
       SET 整体替换值对象——先前持有者冻结旧快照，无撕裂读。
场景 4 show_arm_guard_speed_ratio [context_set_checker / context_turn_on_checker / context_turn_off_checker]
       倍率超 1.0 有撞机风险：挂校验回调后，全局门控未开时 checker 保留
       但不生效；ON 后超限被拒且值不变、合法值放行；OFF（现场调试）门控旁路。
场景 5 show_arm_watch_servo_power [context_set_monitor(同步) / context_turn_on_monitor / context_turn_off_monitor]
       安全 PLC 订阅伺服电源指令：追加式多播（上电告警 + 操作黑匣子）；
       全局开/关控制通知启停——OFF 后 monitor 保留但不执行。
场景 6 show_arm_report_pose      [context_set_monitor(async_monitor=True)]
       数字孪生异步上报末端位姿：单线程池按 set 提交序保序送达，
       执行线程是池线程——上报不阻塞运动控制回路。
场景 7 show_arm_dashboard        [context_info_keys / context_info_key / context_info_all]
       产线运维面板：键名清单、单键结构化元信息（含未命中 eNo 分支）、
       全量导出——框架只回结构化数据，展示格式由用户定义。
场景 8 show_arm_task_cleanup     [context_destroy]
       下电流程逐键销毁；销毁后 GET 回落兜底默认值、重复销毁返回 eNo；
       INFO_KEYS 确认键值表已空，会话闭环。

共享键规划（跨场景流转，模拟多模块共享同一份状态）：
  arm.mode        str    运行模式（idle/teach/auto）
  arm.speed_ratio float  全局速度倍率 0.0~1.0
  arm.servo_power bool   伺服电源指令状态
  arm.tcp_pose    list   末端位姿 [x,y,z,qx,qy,qz,qw]
  arm.payload_kg  float  末端负载 kg（全程未注册，作 GET 兜底演示）

运行方式：PYTHONPATH=src python3 examples/ExampleYomkContext.py
"""

import threading
import time

import YomkApi


# ---------------------------------------------------------------------------
# 辅助输出：全部走 YomkLogger 控制台日志（console_log_info_tag 等），无 print。
# 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
# ---------------------------------------------------------------------------

# Float64 值格式化：固定 1 位小数（速度倍率语义，0.8 显示为 "0.8"、0.0 显示为 "0.0"）
def fmt_f64(value):
    return f"{value:.1f}"


# 末端位姿 7 元组 [x,y,z,qx,qy,qz,qw] 格式化
def fmt_pose(pose):
    return "[" + ", ".join(str(v) for v in pose) + "]"


# 场景横幅：编号 + 标题 + 涉及接口，随后一行场景背景；横幅前留一个空行分隔场景
def log_scene(n, title, apis, explain):
    YomkApi.console_log_info_tag("arm.demo", "")
    YomkApi.console_log_info_tag(
        "arm.demo", f"========== 场景 {n}：{title}（{apis}）==========")
    YomkApi.console_log_info_tag("arm.demo", "场景背景：" + explain)


# API 结果行：打印 YomkResponse 三要素中的 status/msg（数据形态各异，由调用处按需展开）。
# status 三态：0=eOk 成功；1=eNo 名字不存在或被拒绝；-1=eInvalid 参数无效或未初始化
def log_resp(api, resp):
    if resp.status == YomkApi.ResStatus.eOk:
        note = "(eOk 成功)"
    elif resp.status == YomkApi.ResStatus.eNo:
        note = "(eNo 拒绝或不存在)"
    else:
        note = "(eInvalid 未初始化或参数无效)"
    YomkApi.console_log_info_tag(
        "arm.demo", f'[{api}] status={resp.status.value}{note}, msg="{resp.msg}"')


# 操作日志（YomkResponse 版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
# 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
# 结果行标注本次接口调用；同步 checker/monitor 回调插入的日志被夹在开始行与
# 结果行之间，归属一目了然，读者顺着日志读即可理解，无需回看代码
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（GET 等返回值非 YomkResponse 的操作）：由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# GET 结果行：context_get 返回值而非 YomkResponse；
# is_default 由调用方比较返回值与兜底默认值是否 is 同一对象得出
def log_get(key, value_text, is_default):
    suffix = "（兜底默认值）" if is_default else "（命中）"
    YomkApi.console_log_info_tag(
        "arm.demo", f"[GET] {key} = {value_text}{suffix}")


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# 自省结果展开：单键 ContextKeyInfo 的全部字段逐行展示（键名行 + 5 个字段行），
# INFO_KEY 与 INFO_ALL 共用；紧随其结果行输出，键名行以 "-" 起头、字段行缩进对齐
def log_key_info(api, info):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {info.key}")
    # 类型名为空表示键值为空（尚未 set），防御展示
    type_text = info.type_name if info.type_name else "(空)"
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     type: {type_text}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     checker: {'on' if info.checker_on else 'off'}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     monitors: {info.monitor_count}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     async: {info.async_monitor_count}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     group: {info.group}")


# CREATE 幂等化封装：键已存在（eNo "key already exists"）视为就绪跳过。
# 本身即 context_create 的工程化用法：排他 eNo 在正式代码中按"已初始化"
# 处理而非错误——后启动的模块靠它安全补建自己关心的键。
def ensure_key(op, key, value):
    resp = log_action(op, "CREATE " + key, lambda: YomkApi.context_create(key, value))
    if resp.status != YomkApi.ResStatus.eOk:
        YomkApi.console_log_info_tag(
            "arm.demo", f"[ensureKey] {key} 已存在，跳过创建（{resp.msg}）")


# ---------------------------------------------------------------------------
# Checker / Monitor 回调：用户代码，框架在 set 调用链的固定时机调用。
# Checker 在写锁内调用（回调内重入 set/get 会死锁）；Monitor 在写锁外执行。
# ---------------------------------------------------------------------------

# 速度倍率安全门控：倍率是安全关键参数，超过 1.0（100%）有撞机风险，一律拒绝
def checker_speed_ratio(ctx: YomkApi.Context) -> YomkApi.CheckStatus:
    ratio = ctx.value
    if not isinstance(ratio, float):
        YomkApi.console_log_warn_tag(
            "arm.guard", f"checker: {ctx.key} 值不是 float，无法校验 -> eReject")
        return YomkApi.CheckStatus.eReject
    if ratio > 1.0:
        YomkApi.console_log_warn_tag(
            "arm.guard",
            f"checker: {ctx.key}={fmt_f64(ratio)} 超过安全倍率 1.0（撞机风险）-> eReject")
        return YomkApi.CheckStatus.eReject
    YomkApi.console_log_info_tag(
        "arm.guard", f"checker: {ctx.key}={fmt_f64(ratio)} 在安全范围内 -> eAccept")
    return YomkApi.CheckStatus.eAccept


# 安全 PLC 的上电告警：伺服电源指令属安全相关事件，收到快照即告警记录
def monitor_servo_power_alarm(ctx: YomkApi.Context) -> None:
    state = "ON" if ctx.value else "OFF"
    YomkApi.console_log_warn_tag(
        "arm.watch",
        f"[告警] 伺服电源指令: {ctx.key}={state}（安全 PLC 收到本次写入快照）")


# 操作黑匣子：记录每一次伺服电源指令的快照，供事后追溯；
# 与告警回调同批执行，展示 set_monitor 的追加式多播
def monitor_servo_power_log(ctx: YomkApi.Context) -> None:
    state = "ON" if ctx.value else "OFF"
    YomkApi.console_log_info_tag(
        "arm.watch",
        f"[黑匣子] 记录操作: {ctx.key}->{state}（追加式多播：与告警回调按注册序同批执行）")


# 数字孪生位姿上报：async_monitor=True 经单线程池送达（按 set 提交序保序），
# 执行线程是池线程——上报动作不阻塞运动控制回路
def monitor_pose_telemetry(ctx: YomkApi.Context) -> None:
    pose = ctx.value
    if not isinstance(pose, list):
        YomkApi.console_log_warn_tag(
            "arm.telemetry", f"上报失败: {ctx.key} 值不是 list")
        return
    YomkApi.console_log_info_tag(
        "arm.telemetry",
        f"位姿上报: {ctx.key}={fmt_pose(pose)}"
        f"（池线程 {threading.current_thread().name}，按 set 提交序送达）")


# ---------------------------------------------------------------------------
# 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按作业会话顺序编排。
# 全局开关流转：checker/monitor 默认 OFF，各场景自行 arrange 并在结尾恢复
# 默认，避免跨场景干扰。
# ---------------------------------------------------------------------------

# 场景 1：控制柜上电，主控模块把机械臂初始状态写入共享上下文
def show_arm_boot_create():
    log_scene(
        1,
        "控制柜上电初始化",
        "context_create",
        "主控模块把运行模式/速度倍率/伺服状态写入共享上下文，供示教器、安全 PLC 等模块共享；"
        "重复创建同名键会被拒绝——创建是排他的。")

    log_action("写入初始运行模式", "CREATE arm.mode",
               lambda: YomkApi.context_create("arm.mode", "idle"))
    log_action("写入初始速度倍率", "CREATE arm.speed_ratio",
               lambda: YomkApi.context_create("arm.speed_ratio", 0.5))
    log_action("写入初始伺服电源状态", "CREATE arm.servo_power",
               lambda: YomkApi.context_create("arm.servo_power", False))

    # 重复创建同名键：eNo "key already exists"，排他语义——更新请走 SET（见场景 3）
    log_action("重复创建同名键", "CREATE arm.mode (重复)",
               lambda: YomkApi.context_create("arm.mode", "auto"))
    log_verify("重复创建同名键 -> eNo(key already exists)：创建排他，更新请用 SET")


# 场景 2：示教器 HMI 读取机械臂状态
def show_arm_read_status():
    log_scene(
        2,
        "示教器读取状态",
        "context_get",
        "示教器启动时读取机械臂状态：命中返回只读快照；负载称重模块尚未上线，查询其键返回"
        "兜底默认值——调用方永不拿到空值。")

    # 兜底默认值：GET 未命中时原样返回该值（按值兜底，按 is 同一性识别兜底路径）
    ratio_fallback = 0.0

    log_action_start("示教器读取速度倍率")
    ratio = YomkApi.context_get("arm.speed_ratio", ratio_fallback)
    log_get("arm.speed_ratio", fmt_f64(ratio), ratio is ratio_fallback)

    log_action_start("示教器查询未上线的负载模块")
    payload = YomkApi.context_get("arm.payload_kg", ratio_fallback)
    log_get("arm.payload_kg", fmt_f64(payload), payload is ratio_fallback)
    log_verify("查询未注册的键 -> 返回兜底默认值 0.0（is 同一对象），调用方永不拿到空值")


# 场景 3：机械臂切自动模式并提速
def show_arm_update_status():
    log_scene(
        3,
        "运行参数调整",
        "context_set",
        "切入自动模式并提速：SET 整体替换值对象，先前 GET 的持有者仍是旧快照（无撕裂读）；"
        "值类型必须与创建时一致，误写类型会被拒绝。")

    # 示教器先前读到的旧快照（0.5）——SET 之后它不应被改动
    ratio_fallback = 0.0
    log_action_start("示教器留存当前倍率快照")
    old_snapshot = YomkApi.context_get("arm.speed_ratio", ratio_fallback)
    log_get("arm.speed_ratio", fmt_f64(old_snapshot), old_snapshot is ratio_fallback)

    log_action("切入自动模式", "SET arm.mode",
               lambda: YomkApi.context_set("arm.mode", "auto"))
    log_action("提高速度倍率至 0.8", "SET arm.speed_ratio",
               lambda: YomkApi.context_set("arm.speed_ratio", 0.8))

    # 新旧快照对比：验证 SET 的整体替换语义（业务结论，不限于单次接口调用）
    log_action_start("新旧快照对比")
    fresh = YomkApi.context_get("arm.speed_ratio", ratio_fallback)
    log_get("arm.speed_ratio 旧快照", fmt_f64(old_snapshot), old_snapshot is ratio_fallback)
    log_get("arm.speed_ratio 新快照", fmt_f64(fresh), fresh is ratio_fallback)
    log_verify("SET 整体替换值对象：留存快照保持 0.5 不变，新读取得到 0.8——旧快照即冻结")

    # 强类型契约：arm.mode 是 str，误用 float 类型写入被拒
    log_action("误用类型改写运行模式", "SET arm.mode (float)",
               lambda: YomkApi.context_set("arm.mode", 1.0))
    log_verify("误用类型改写运行模式 -> eNo(context type not match)：强类型防误写")


# 场景 4：速度倍率安全门控（速度倍率是安全关键参数）
def show_arm_guard_speed_ratio():
    log_scene(
        4,
        "速度倍率安全门控",
        "context_set_checker / context_turn_on_checker / context_turn_off_checker",
        "倍率超过 1.0（100%）有撞机风险：set_checker 挂校验回调；全局门控未开时 checker "
        "保留但不生效；ON 后超限写入被拒且值不变、合法值放行；OFF（现场调试模式）门控旁路。")

    # 挂校验回调：仅注册。全局开关默认 OFF，此时 checker 保留在键上但不生效
    log_action("挂载倍率安全校验", "SET_CHECKER arm.speed_ratio",
               lambda: YomkApi.context_set_checker("arm.speed_ratio", checker_speed_ratio))
    log_action("门控未开时尝试超限写入", "SET arm.speed_ratio=1.2",
               lambda: YomkApi.context_set("arm.speed_ratio", 1.2))
    log_verify("校验已挂但全局门控未开 -> 1.2 照样写入（注意 arm.guard 日志未出现）")

    # 打开全局门控：合法值放行，超限被拒且值不变
    log_action("打开全局校验门控", "ON_CHECKER", lambda: YomkApi.context_turn_on_checker())
    log_action("写入合法倍率 0.9", "SET arm.speed_ratio=0.9",
               lambda: YomkApi.context_set("arm.speed_ratio", 0.9))
    log_verify("门控打开后合法值 0.9 -> arm.guard 日志 eAccept，写入成功")
    log_action("尝试写入超限倍率 1.2", "SET arm.speed_ratio=1.2",
               lambda: YomkApi.context_set("arm.speed_ratio", 1.2))
    log_action_start("确认被拒后倍率保持不变")
    ratio_fallback = 0.0
    ratio = YomkApi.context_get("arm.speed_ratio", ratio_fallback)
    log_get("arm.speed_ratio", fmt_f64(ratio), ratio is ratio_fallback)
    log_verify("超限写入 -> eNo(checker reject set context) 且值保持 0.9 未变；arm.guard 日志给出拒绝理由")

    # 关闭全局门控（现场调试模式）：门控旁路，checker 仍在键上
    log_action("现场调试关闭门控", "OFF_CHECKER", lambda: YomkApi.context_turn_off_checker())
    log_action("门控关闭后再次写入 1.2", "SET arm.speed_ratio=1.2",
               lambda: YomkApi.context_set("arm.speed_ratio", 1.2))
    log_verify("门控关闭后 1.2 -> 写入成功：门控旁路，校验保留但不生效")

    # 收尾恢复安全值，并保持全局开关为默认 OFF
    log_action("恢复安全倍率 0.5", "SET arm.speed_ratio=0.5",
               lambda: YomkApi.context_set("arm.speed_ratio", 0.5))


# 场景 5：安全 PLC 订阅伺服电源指令
def show_arm_watch_servo_power():
    log_scene(
        5,
        "伺服上下电订阅",
        "context_set_monitor(同步) / context_turn_on_monitor / context_turn_off_monitor",
        "安全 PLC 订阅伺服电源指令：set_monitor 追加式注册两个同步回调（上电告警 + 操作"
        "黑匣子）；全局监控 ON 后每次写入成功即通知，OFF 后通知暂停但 monitor 保留。")

    # 追加式多播：同一键可挂多个 monitor，按注册顺序全部执行
    log_action("安全 PLC 订阅上电告警", "SET_MONITOR arm.servo_power",
               lambda: YomkApi.context_set_monitor("arm.servo_power", monitor_servo_power_alarm))
    log_action("安全 PLC 订阅操作黑匣子", "SET_MONITOR arm.servo_power",
               lambda: YomkApi.context_set_monitor("arm.servo_power", monitor_servo_power_log))

    # 对照：全局监控默认 OFF，写入静默（注意观察无 arm.watch 日志）
    log_action("全局监控关闭时下发上电指令", "SET arm.servo_power=true",
               lambda: YomkApi.context_set("arm.servo_power", True))
    log_verify("全局监控默认 OFF -> 上电指令写入成功但无 arm.watch 日志（monitor 保留但不执行）")

    log_action("开启全局监控", "ON_MONITOR", lambda: YomkApi.context_turn_on_monitor())

    # 同步通知在 set 返回前（写锁外）执行：arm.watch 日志会夹在"开始执行"与
    # 结果行之间，属预期时序——顺着日志读即可看出通知归属这次操作
    log_action("下发下电指令", "SET arm.servo_power=false",
               lambda: YomkApi.context_set("arm.servo_power", False))
    log_verify("监控开启后写入 -> 两条 arm.watch 日志夹在开始行与结果行之间（同步通知写锁外、set 返回前执行）")

    log_action("暂停全局监控", "OFF_MONITOR", lambda: YomkApi.context_turn_off_monitor())
    log_action("再上电验证通知已暂停", "SET arm.servo_power=true",
               lambda: YomkApi.context_set("arm.servo_power", True))
    log_verify("监控暂停后写入 -> 无 arm.watch 日志：通知暂停，monitor 保留在键上")

    # 收尾保持全局监控 OFF 默认；场景 6 自行按需开启


# 场景 6：数字孪生异步上报末端位姿
def show_arm_report_pose():
    log_scene(
        6,
        "末端位姿遥测上报",
        "context_set_monitor(async_monitor=True)",
        "数字孪生订阅末端位姿：async_monitor=True 的 monitor 经单线程池送达（按 set 提交序"
        "保序），执行线程是池线程——上报不阻塞运动控制回路。")

    # 位姿键可能晚于主控创建（数字孪生模块后启动），幂等补建
    ensure_key("数字孪生创建位姿", "arm.tcp_pose", [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0])

    # arrange：全局监控在场景 5 收尾已恢复 OFF，此处按需开启
    log_action("开启全局监控", "ON_MONITOR", lambda: YomkApi.context_turn_on_monitor())
    log_action("数字孪生订阅异步位姿上报", "SET_MONITOR arm.tcp_pose (async)",
               lambda: YomkApi.context_set_monitor("arm.tcp_pose", monitor_pose_telemetry, True))

    YomkApi.console_log_info_tag(
        "arm.demo", f"主线程 id: {threading.current_thread().name}")

    # 连续 3 次位姿更新（模拟插补运动）：提交序 #1 -> #2 -> #3
    log_action("插补运动 #1", "SET arm.tcp_pose",
               lambda: YomkApi.context_set("arm.tcp_pose", [100.0, 0.0, 200.0, 0.0, 0.0, 0.0, 1.0]))
    log_action("插补运动 #2", "SET arm.tcp_pose",
               lambda: YomkApi.context_set("arm.tcp_pose", [120.0, 40.0, 220.0, 0.0, 0.0, 0.707, 0.707]))
    log_action("插补运动 #3", "SET arm.tcp_pose",
               lambda: YomkApi.context_set("arm.tcp_pose", [150.0, 80.0, 250.0, 0.0, 0.0, 1.0, 0.0]))

    YomkApi.console_log_info_tag("arm.demo", "等待异步上报送达（单线程池按提交序执行）...")
    time.sleep(0.3)
    log_verify("3 条 arm.telemetry 日志按插补序 #1->#2->#3 到达且线程为池线程（与主线程不同）——不阻塞控制回路")

    # 收尾恢复全局监控 OFF 默认
    log_action("停止位姿上报", "OFF_MONITOR", lambda: YomkApi.context_turn_off_monitor())


# 场景 7：产线运维面板巡检机械臂共享状态
def show_arm_dashboard():
    log_scene(
        7,
        "产线运维面板",
        "context_info_keys / context_info_key / context_info_all",
        "运维面板巡检：INFO_KEYS 拉取键名清单；INFO_KEY 抽查单键注册详情（结构化 "
        "ContextKeyInfo）；INFO_ALL 全量导出——框架只回结构化数据，展示格式由用户定义。")

    # 键名清单（结构化列表）
    keys_resp = log_action("巡检键名清单", "INFO_KEYS", lambda: YomkApi.context_info_keys())
    if keys_resp.data:
        for key in keys_resp.data:
            YomkApi.console_log_info_tag("arm.demo", "[INFO_KEYS] - " + key)

    # 单键抽查：速度倍率——场景 4 挂的门控回调仍在
    ratio_resp = log_action("抽查速度倍率注册详情", "INFO_KEY arm.speed_ratio",
                            lambda: YomkApi.context_info_key("arm.speed_ratio"))
    if ratio_resp.data is not None:
        log_key_info("INFO_KEY", ratio_resp.data)
    log_verify("arm.speed_ratio：checker=on（场景 4 的门控回调仍在）")

    # 单键抽查：末端位姿——异步上报 monitor 已追加
    pose_resp = log_action("抽查末端位姿注册详情", "INFO_KEY arm.tcp_pose",
                           lambda: YomkApi.context_info_key("arm.tcp_pose"))
    if pose_resp.data is not None:
        log_key_info("INFO_KEY", pose_resp.data)
    log_verify("arm.tcp_pose：monitors=1、async=1（场景 6 的异步上报仍在，async 是 monitors 中异步的数量）")

    # 未命中惯例：查询未注册键返回 eNo
    log_action("查询未注册键", "INFO_KEY arm.ghost",
               lambda: YomkApi.context_info_key("arm.ghost"))
    log_verify("查询未注册的键 -> eNo(key is not exist)")

    # 全量导出（组 → 键元信息两层结构化列表），逐组逐键展开全部字段
    all_resp = log_action("全量导出键值表", "INFO_ALL", lambda: YomkApi.context_info_all())
    if all_resp.data:
        for group in all_resp.data:
            for info in group.keys:
                log_key_info("INFO_ALL", info)


# 场景 8：机械臂下电流程，逐键清理共享状态
def show_arm_task_cleanup():
    log_scene(
        8,
        "下电清理",
        "context_destroy",
        "下电流程逐键销毁共享状态；销毁后一切操作按 not-found 惯例返回 eNo，"
        "GET 回落兜底默认值。")

    log_action("销毁末端位姿键", "DESTROY arm.tcp_pose",
               lambda: YomkApi.context_destroy("arm.tcp_pose"))
    log_action("销毁伺服电源键", "DESTROY arm.servo_power",
               lambda: YomkApi.context_destroy("arm.servo_power"))
    log_action("销毁速度倍率键", "DESTROY arm.speed_ratio",
               lambda: YomkApi.context_destroy("arm.speed_ratio"))
    log_action("销毁运行模式键", "DESTROY arm.mode",
               lambda: YomkApi.context_destroy("arm.mode"))

    # 销毁后行为：GET 回落兜底默认值
    mode_fallback = "unknown"
    log_action_start("确认销毁后读取回落")
    mode = YomkApi.context_get("arm.mode", mode_fallback)
    log_get("arm.mode", mode, mode is mode_fallback)
    log_verify("销毁后 GET -> 返回兜底默认值（is 同一对象），键已消失")

    # 重复销毁：not-found 惯例
    log_action("重复销毁运行模式键", "DESTROY arm.mode (重复)",
               lambda: YomkApi.context_destroy("arm.mode"))
    log_verify("重复销毁同名键 -> eNo(key is not exist)")

    # 收尾确认：键值表已空，会话闭环
    keys_resp = log_action("收尾确认键值表已空", "INFO_KEYS", lambda: YomkApi.context_info_keys())
    log_verify("键值表已空：机械臂会话结束，共享状态全部下线"
               if not keys_resp.data else "键值表非空：存在未清理键")


def main():
    # 初始化框架（上下文服务 /YomkContext 随之自动启动）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂控制 x YomkContext 示例：一次作业会话贯穿 8 个场景，覆盖全部上下文 API")
    YomkApi.console_log_info_tag(
        "arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.guard=校验 / arm.watch=通知 / arm.telemetry=上报")

    show_arm_boot_create()
    show_arm_read_status()
    show_arm_update_status()
    show_arm_guard_speed_ratio()
    show_arm_watch_servo_power()
    show_arm_report_pose()
    show_arm_dashboard()
    show_arm_task_cleanup()

    YomkApi.console_log_info_tag("arm.demo", "========== 示例结束：按回车退出 ==========")
    input()


if __name__ == "__main__":
    main()
