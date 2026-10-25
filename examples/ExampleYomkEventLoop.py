#!/usr/bin/env python3
"""
ExampleYomkEventLoop —— YomkEventLoop 线程隔离事件循环示例：机械臂产线班次背景的 8 个场景小示例（面向初学者）

事件循环为特定业务提供一条单线程 FIFO 执行队列：投递进同一循环的
事件按入队顺序在该循环的专用线程中逐个执行，循环之间彼此并行，
适合把"必须串行"的业务（如运动指令序列、示教器操作流水）从锁竞争里解耦出来。

本示例用"机械臂产线一次班次"贯穿 8 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部 8 个 event_loop_* 用户接口无一遗漏。

日志统一走 YomkLogger 并按 tag 分层（arm.demo 叙事与结果 / arm.motion 运动
循环线程 / arm.teach 示教循环线程），日志流即执行序、顺着读就能明白：
每个操作先打"开始执行 xxx"——操作是用户视角的业务动作（可以是一个接口
调用，也可以是多个接口调用组合成的完整动作），随后结果行标注本次接口
调用；循环线程执行的日志穿插其间，线程名自证执行位置。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_line_start      [event_loop_start(3参) / event_loop_info_loop]
       产线开班：启动运动指令循环（默认处理函数 + msg_name 元数据），INFO_LOOP
       确认专用线程就绪；重复 START 同名循环为幂等重启（eOk 不报错）。
场景 2 show_arm_motion_queue    [event_loop_post / event_loop_info_loop(n)]
       连续下发 5 条运动指令：POST 立即返回不阻塞；INFO_LOOP(n) 观察队列
       积压与队首 tag 预览；第 1 条指令执行时递归投递 3 条微调指令排到
       队尾——消费顺序自证同循环 FIFO。
场景 3 show_arm_dual_loop       [event_loop_start(2参) / event_loop_post]
       双循环并行：示教器循环用 2 参 START（默认处理函数、无 msg_name 元数据），
       与运动循环同时投递——两循环日志交错、线程名不同，同循环 FIFO、
       跨循环彼此并行。
场景 4 show_arm_motion_sync_wait [event_loop_post_wait]
       回原点是关键动作：POST_WAIT 阻塞至事件在循环线程执行完毕才返回，
       Event 包三要素回传（eventId 单调递增 / loopName / 处理函数返回值）。
场景 5 show_arm_temp_handler    [event_loop_post_wait(临时函数)]
       紧急巡检例程投递时指定临时处理函数：优先于默认函数，返回临时函数
       的结果——同一循环可混用两种来源。
场景 6 show_arm_line_pause      [event_loop_stop / event_loop_start(续跑) / event_loop_post]
       班中暂停：STOP 只停线程、保留队列事件；停止后投递被拒；
       再次 START 续跑消化保留事件。
场景 7 show_arm_dashboard       [event_loop_info_loops / event_loop_info_loop / event_loop_info_all]
       产线运维面板：循环名清单、单循环详情（含队列 tag 预览）、
       全量导出——框架只回结构化数据，展示格式由用户定义。
场景 8 show_arm_line_off        [event_loop_destroy]
       收班：逐循环 DESTROY（停线程并清空队列、移出循环表，不可续跑）；
       对已销毁循环的一切操作均返回 eNo（not-found 惯例），清单确认已空。

循环规划（跨场景流转，模拟主控/示教器两模块各自专用线程）：
  motionLoop  START 3 参（motion_handle + "String" 元数据）  主控运动指令循环
  teachLoop   START 2 参（teach_handle，无 msg_name 元数据） 示教器循环

运行方式：PYTHONPATH=src python3 examples/ExampleYomkEventLoop.py
"""

import threading
import time

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
# 结果行标注本次接口调用；循环线程执行的日志穿插其间，读者顺着日志读即可
# 理解，无需回看代码
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（返回值需要定制展开的动作，如 INFO_LOOP/INFO_LOOPS/INFO_ALL）：
# 由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# 自省结果展开：单循环 EventLoopInfo 的全部字段逐行展示（循环名行 + 字段行），
# INFO_LOOP 与 INFO_ALL 共用；紧随其结果行输出，循环名行以 "-" 起头、字段行缩进对齐
def log_loop_info(api, info):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {info.loop_name}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     running: {'on' if info.running else 'off'}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     pending: {info.pending}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[{api}]     defaultFunc: {'on' if info.has_default_func else 'off'}")
    # msg_name 为空表示 2 参 START（未指定元数据），防御展示
    msg_text = info.msg_name if info.msg_name else "(未指定)"
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     msgName: {msg_text}")
    tags = ", ".join(t if t else "-" for t in info.next_tags)
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     nextTags: [{tags}]")


# POST_WAIT 回传的 Event 包逐字段展开：eventId / loopName / response（处理函数返回值）
def log_event(resp):
    event = resp.data
    if event is None:
        return
    YomkApi.console_log_info_tag("arm.demo", f"[Event] - eventId: {event.m_eventId}")
    YomkApi.console_log_info_tag(
        "arm.demo", f"[Event]     loopName: {event.m_eventLoopName}")
    if event.m_response is not None:
        YomkApi.console_log_info_tag(
            "arm.demo",
            f'[Event]     response: status={event.m_response.status.value}, msg="{event.m_response.msg}"')


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# INFO_LOOPS 清单行：循环名列表逐行展示（- 无序列表风格）
def log_loop_line(api, loop_name):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {loop_name}")


# ---------------------------------------------------------------------------
# 事件处理函数：签名统一为 YomkResponse(pkg)。
# 可作为默认处理函数（启动时指定）或临时处理函数（投递时指定）。
# ---------------------------------------------------------------------------

# 运动指令处理函数：默认处理函数（场景 1 START 时指定）。
# 模拟 200ms 运动耗时，让队列积压与 FIFO 消费顺序在日志中稳定可见；
# 第 1 条指令执行时递归投递 3 条微调指令排到队尾——同循环 FIFO 的直观证据
def motion_handle(pkg) -> YomkApi.YomkResponse:
    YomkApi.console_log_info_tag(
        "arm.motion", f"执行运动指令: {pkg}（循环线程 {threading.current_thread().name}）")

    time.sleep(0.2)

    # 演示处理函数内递归投递：仅 mv1 会追加 3 条微调指令到队尾，
    # 它们排在已入队的 mv2..mv5 之后消费——同循环 FIFO 的直观证据
    if pkg == "mv1":
        for i in range(1, 4):
            YomkApi.event_loop_post("motionLoop", "fine" + str(i), None, "fine" + str(i))
        YomkApi.console_log_info_tag("arm.motion", "指令 mv1 触发 3 条微调指令，已追加到队列尾部")

    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "eventHandle success. ")


# 示教器事件处理函数：轻量打印，供双循环并行演示（场景 3 START 2 参时指定）
def teach_handle(pkg) -> YomkApi.YomkResponse:
    YomkApi.console_log_info_tag(
        "arm.teach", f"示教器事件: {pkg}（循环线程 {threading.current_thread().name}）")
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "teachHandle success")


# ---------------------------------------------------------------------------
# 8 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
# 循环流转：motionLoop 场景 1 创建、teachLoop 场景 3 创建，场景 8 一并销毁。
# ---------------------------------------------------------------------------

# 场景 1：产线开班，主控模块启动运动指令循环
def show_arm_line_start():
    log_scene(
        1,
        "产线开班启动运动循环",
        "event_loop_start(3参) / event_loop_info_loop",
        "开班时主控模块启动运动指令循环：START 为该名称新建一条专用线程并注册默认处理函数，"
        "msg_name 仅作内省元数据；对已存在名称再次 START 是幂等重启。")

    log_action("启动运动指令循环", "START motionLoop",
               lambda: YomkApi.event_loop_start("motionLoop", motion_handle, "String"))

    log_action_start("查看循环运行状态")
    resp = YomkApi.event_loop_info_loop("motionLoop")
    log_resp("INFO_LOOP motionLoop", resp)
    if resp.data is not None:
        log_loop_info("INFO_LOOP", resp.data)
    log_verify("running=on、defaultFunc=on：专用线程已就绪，未指定函数的事件由 motion_handle 处理")

    # 重复启动同名循环：幂等重启，不报错也不影响线程与队列
    log_action("重复启动同名循环", "START motionLoop (重复)",
               lambda: YomkApi.event_loop_start("motionLoop", motion_handle, "String"))
    log_verify("对已存在名称再次 START -> eOk：幂等重启，线程与队列不受影响")


# 场景 2：插补运动连续下发（异步投递 + 队列观察 + FIFO）
def show_arm_motion_queue():
    log_scene(
        2,
        "连续下发运动指令",
        "event_loop_post / event_loop_info_loop(n)",
        "插补运动连续下发：POST 把事件放进循环队列后立即返回，不阻塞运动控制线程；"
        "tag 仅内省可见，用于观察队列积压；同循环内事件按入队顺序（FIFO）执行。")

    # 连续 5 条指令（每条处理 200ms）：POST 立即返回，指令在循环线程排队执行
    for i in range(1, 6):
        cmd = "mv" + str(i)
        log_action("下发运动指令 " + cmd, "POST motionLoop",
                   lambda cmd=cmd: YomkApi.event_loop_post("motionLoop", cmd, None, cmd))

    log_action_start("立即观察队列积压")
    resp = YomkApi.event_loop_info_loop("motionLoop", 5)
    log_resp("INFO_LOOP motionLoop(5)", resp)
    if resp.data is not None:
        log_loop_info("INFO_LOOP", resp.data)
    log_verify("正在执行的事件不计入 pending，nextTags 列出待处理指令的 tag——POST 已返回而指令仍在排队")

    YomkApi.console_log_info_tag(
        "arm.demo", "等待队列排空，观察 arm.motion 日志的消费顺序：mv2..mv5 之后才是 fine1..fine3...")
    time.sleep(1.8)

    log_action_start("排空后复查")
    resp = YomkApi.event_loop_info_loop("motionLoop", 5)
    log_resp("INFO_LOOP motionLoop(5)", resp)
    if resp.data is not None:
        log_loop_info("INFO_LOOP", resp.data)
    log_verify("mv1 执行中触发的 fine1..fine3 排到已入队指令之后消费——同循环 FIFO，递归投递不插队")


# 场景 3：双循环并行（示教器独立于主控）
def show_arm_dual_loop():
    log_scene(
        3,
        "双循环并行",
        "event_loop_start(2参) / event_loop_post",
        "示教器模块独立于主控：teachLoop 用 2 参 START（默认处理函数、无 msg_name 元数据）；"
        "两个循环各自专用线程，同循环内 FIFO、跨循环彼此并行。")

    log_action("启动示教器循环", "START teachLoop",
               lambda: YomkApi.event_loop_start("teachLoop", teach_handle))

    YomkApi.console_log_info_tag(
        "arm.demo", f"主线程 id: {threading.current_thread().name}")

    # 两循环交错投递：各自专用线程并行消费，日志交错出现；
    # 操作行带指令名，与循环线程的消费日志一一对应
    log_action("主控下发轨迹指令 track1", "POST motionLoop",
               lambda: YomkApi.event_loop_post("motionLoop", "track1", None, "track1"))
    log_action("示教器记录示教点 point1", "POST teachLoop",
               lambda: YomkApi.event_loop_post("teachLoop", "point1", None, "point1"))
    log_action("主控下发轨迹指令 track2", "POST motionLoop",
               lambda: YomkApi.event_loop_post("motionLoop", "track2", None, "track2"))
    log_action("示教器记录示教点 point2", "POST teachLoop",
               lambda: YomkApi.event_loop_post("teachLoop", "point2", None, "point2"))

    YomkApi.console_log_info_tag("arm.demo", "等待两循环消费（各 2 条 x 200ms）...")
    time.sleep(0.6)
    log_verify("arm.motion 与 arm.teach 日志交错出现且线程名均不同于主线程——跨循环彼此并行")


# 场景 4：关键指令同步等待（POST_WAIT）
def show_arm_motion_sync_wait():
    log_scene(
        4,
        "关键指令同步等待",
        "event_loop_post_wait",
        "回原点是关键动作，必须确认执行完毕才能继续：POST_WAIT 阻塞至事件在循环线程"
        "执行完毕，回传 Event 包三要素（eventId / loopName / 处理函数返回值）。")

    resp = log_action("下发回原点指令并等待完成", "POST_WAIT motionLoop",
                      lambda: YomkApi.event_loop_post_wait("motionLoop", "goHome"))
    log_event(resp)
    log_verify("POST_WAIT 返回时回原点已在循环线程执行完毕（耗时 200ms），Event 包携带执行结果")


# 场景 5：紧急任务临时处理函数（优先于默认函数）
def show_arm_temp_handler():
    log_scene(
        5,
        "紧急任务临时处理函数",
        "event_loop_post_wait(临时函数)",
        "安全巡检例程无需占用运动处理函数：投递时指定临时处理函数则优先于默认函数，"
        "返回临时函数的结果——同一循环可混用两种来源。")

    def temp_handle(_pkg) -> YomkApi.YomkResponse:
        return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "tempHandle result")

    resp = log_action("投递紧急巡检任务（临时函数）", "POST_WAIT motionLoop (temp_handle)",
                      lambda: YomkApi.event_loop_post_wait("motionLoop", "safetyCheck", temp_handle))
    log_event(resp)
    log_verify("response 为临时函数的结果（tempHandle result）而非默认函数结果：临时函数优先于默认函数")


# 场景 6：班中暂停与恢复（STOP / START 续跑）
def show_arm_line_pause():
    log_scene(
        6,
        "班中暂停与恢复",
        "event_loop_post / event_loop_stop / event_loop_start(续跑)",
        "换班暂停：STOP 只停线程、保留队列事件；停止后投递被拒；再次 START 续跑消化保留事件。")

    # 投递 2 条积压指令后立即暂停：线程停止但事件保留在队列
    log_action("下发待消化指令 hold1", "POST motionLoop",
               lambda: YomkApi.event_loop_post("motionLoop", "hold1", None, "hold1"))
    log_action("下发待消化指令 hold2", "POST motionLoop",
               lambda: YomkApi.event_loop_post("motionLoop", "hold2", None, "hold2"))
    log_action("暂停运动循环", "STOP motionLoop",
               lambda: YomkApi.event_loop_stop("motionLoop"))

    log_action_start("查看暂停后状态")
    resp = YomkApi.event_loop_info_loop("motionLoop")
    log_resp("INFO_LOOP motionLoop", resp)
    if resp.data is not None:
        log_loop_info("INFO_LOOP", resp.data)
    log_verify("running=off 且积压事件保留在队列：STOP 只停线程不清队列，可续跑")

    log_action("停止态投递被拒", "POST motionLoop (已停止)",
               lambda: YomkApi.event_loop_post("motionLoop", "rejected"))
    log_verify("停止后投递 -> eNo(event loop not running, post rejected)：循环未运行不接收新事件")

    log_action("恢复运行（续跑）", "START motionLoop (续跑)",
               lambda: YomkApi.event_loop_start("motionLoop", motion_handle, "String"))
    YomkApi.console_log_info_tag("arm.demo", "等待保留事件被续跑消化...")
    time.sleep(0.6)

    log_action_start("确认积压已消化")
    resp = YomkApi.event_loop_info_loop("motionLoop")
    log_resp("INFO_LOOP motionLoop", resp)
    if resp.data is not None:
        log_loop_info("INFO_LOOP", resp.data)
    log_verify("续跑后保留事件按 FIFO 消化完毕，pending 归零：STOP/START 不丢事件")


# 场景 7：产线运维面板巡检
def show_arm_dashboard():
    log_scene(
        7,
        "产线运维面板",
        "event_loop_info_loops / event_loop_info_loop / event_loop_info_all",
        "运维面板巡检：INFO_LOOPS 拉取循环名清单；INFO_LOOP 单查循环详情（含队列 tag "
        "预览）；INFO_ALL 全量导出——框架只回结构化数据，展示格式由用户定义。")

    # 循环名清单（结构化列表）
    log_action_start("巡检循环名清单")
    loops_resp = YomkApi.event_loop_info_loops()
    log_resp("INFO_LOOPS", loops_resp)
    if loops_resp.data:
        for loop_name in loops_resp.data:
            log_loop_line("INFO_LOOPS", loop_name)

    # 单循环抽查：运动循环（含队列 tag 预览，队列空时 nextTags 为空表）
    log_action_start("抽查运动循环详情")
    loop_resp = YomkApi.event_loop_info_loop("motionLoop", 3)
    log_resp("INFO_LOOP motionLoop(3)", loop_resp)
    if loop_resp.data is not None:
        log_loop_info("INFO_LOOP", loop_resp.data)
    log_verify("motionLoop：running=on、defaultFunc=on（场景 1 注册的默认处理函数仍在）")

    # 全量导出（结构化列表），逐循环展开全部字段
    log_action_start("全量导出循环状态")
    all_resp = YomkApi.event_loop_info_all()
    log_resp("INFO_ALL", all_resp)
    if all_resp.data:
        for info in all_resp.data:
            log_loop_info("INFO_ALL", info)


# 场景 8：收班下线（DESTROY 与 not-found 惯例）
def show_arm_line_off():
    log_scene(
        8,
        "收班销毁",
        "event_loop_destroy",
        "收班下线全部循环：DESTROY 停线程并清空队列、把循环移出循环表，不可续跑；"
        "对已销毁循环的一切操作均返回 eNo（not-found 惯例）。")

    log_action("销毁运动循环", "DESTROY motionLoop",
               lambda: YomkApi.event_loop_destroy("motionLoop"))
    log_action("销毁示教循环", "DESTROY teachLoop",
               lambda: YomkApi.event_loop_destroy("teachLoop"))

    log_action_start("巡检清单确认已清空")
    loops_resp = YomkApi.event_loop_info_loops()
    log_resp("INFO_LOOPS", loops_resp)
    log_verify("循环清单已空：机械臂班次结束，全部专用线程已下线"
               if not loops_resp.data else "循环清单非空：存在未销毁循环")

    log_action("已销毁循环投递被拒", "POST motionLoop (已销毁)",
               lambda: YomkApi.event_loop_post("motionLoop", "any"))
    log_action("已销毁循环停止被拒", "STOP motionLoop (已销毁)",
               lambda: YomkApi.event_loop_stop("motionLoop"))
    log_action_start("已销毁循环查询被拒")
    resp = YomkApi.event_loop_info_loop("motionLoop")
    log_resp("INFO_LOOP motionLoop (已销毁)", resp)
    log_verify("对已销毁循环的一切操作 -> eNo(event loop not exist)：not-found 惯例，销毁不可续跑")


def main():
    # 初始化框架（事件循环服务 /YomkEventLoop 随之自动启动）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂产线 x YomkEventLoop 示例：一次班次贯穿 8 个场景，覆盖全部事件循环 API")
    YomkApi.console_log_info_tag(
        "arm.demo", "日志按 tag 分层：arm.demo=叙事与结果 / arm.motion=运动循环线程 / arm.teach=示教循环线程")

    show_arm_line_start()
    show_arm_motion_queue()
    show_arm_dual_loop()
    show_arm_motion_sync_wait()
    show_arm_temp_handler()
    show_arm_line_pause()
    show_arm_dashboard()
    show_arm_line_off()

    YomkApi.console_log_info_tag("arm.demo", "========== 示例结束：按回车退出 ==========")
    input()


if __name__ == "__main__":
    main()
