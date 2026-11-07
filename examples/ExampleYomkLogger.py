#!/usr/bin/env python3
"""
ExampleYomkLogger —— YomkLogger 日志系统示例：机械臂产线背景的 6 个场景小示例（面向初学者）

日志系统提供开箱即用的双路输出：控制台日志（四级级别 + tag 分层 + 代理拦截）
与文件日志（内存缓冲 + 显式刷盘），并附带日志器内省。随 server().init() 拉起内置服务自动启动。
机械臂产线中运行足迹走控制台（班前点检、班中预警、班后清档），关键事件另存
黑匣子与班次存档（文件日志），运维班前班后用内看点检日志系统本身。

控制台输出格式四段：[时间] [级别] [tag] [行号] 内容——tag 即控制台日志器名，
首次使用自动创建；门面自动把 [行号] 拼进内容开头，方便定位调用处。
级别开关两级主闸并联：无参作用于全局主闸（框架全局）、具名逐器门控单个日志器，
输出取两层 AND；控制台与文件两套主闸相互独立（/YomkLogger/on·off_file_log_by_level 操作文件侧），
两套代理亦相互独立（set_console_log_proxy / set_file_log_proxy 各管一侧）。

本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部日志用户门面 API（控制台 8 / 开关 16 / 代理 2 / 文件 10 / 内省 4）无一遗漏；
开关含控制台与文件两族各 8（本示例演示控制台族，文件族 on·off_file_log_by_level 同构）。
叙事日志统一走 arm.demo（YomkLogger 自身即演示对象）。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_log_levels   [log().info/warn/error/debug（六级）+ tag 形态]
       班前点检报文分级：四级日志按 DEBUG 详单 -> INFO 通报 -> WARN 预警 ->
       ERROR 告警递进；默认 tag 与自定义 tag 双填兼容，[行号] 随行定位。
场景 2 show_arm_silent_mode  [/YomkLogger/on·off_console_log_by_level（无参=主闸/具名=逐器） + console_global_state + logger]
       产线静默模式：全局静默四级别逐级全关（空窗由 WARN/ERROR 锚点桥接，调用
       仍返回 eOk）；逐级恢复后单查 main 状态段复查四级别全 on；随后
       逐器静默——具名关断 arm.joint 仅其报文消失（arm.demo 对照不受影响），
       console_global_state 报主闸全 on 与条目 info_off 两层归因唯一。
场景 3 show_arm_blackbox     [/YomkLogger/set_console_log_proxy / set_file_log_proxy / create_file_logger / write_file_log]
       安全黑匣子：代理把 WARN/ERROR 转写文件后消费、DEBUG 噪声消化、INFO
       放行——结果行 msg 区分消费与放行；传 None 卸载恢复默认。随后安装
       文件代理拦 DEBUG 不落盘、None 卸载恢复写入——两套代理相互独立。
场景 4 show_arm_shift_log    [log().to_sink().info/warn/error/debug（六级 sink 形态）+ /YomkLogger/on·off_file_log_by_level + file_global_state]
       班次存档：双 tag 四级写入内存缓冲，显式 WRITE 刷盘；控制台 DEBUG 全关
       期间文件 DEBUG 照常落盘，文件具名关断期间 INFO 静默吞条（主闸仍 on，
       缺席归因逐器门控层）——文件开关与控制台开关两套相互独立。
场景 5 show_arm_logger_dashboard [/YomkLogger/loggers / logger / all / console·file_global_state]
       运维面板：console/file 清单（字典序）、单查结构化 LoggerInfo（含未
       注册 eNo）、全量导出——条目各自报逐器门控层真值与对应侧代理态，
       console·file 两个 global_state 端点分别单查两套主闸态，两层一起看全。
场景 6 show_arm_log_cleanup  [/YomkLogger/delete_logger + 惰性重建]
       收班清档：DELETE 同名清 console+file 两表（返回清了几张表）、磁盘
       .log 保留；main 删除后再写自动重建（按名惰性创建）。

tag 规划（arm.demo 叙事之外，演示对象按业务分层）：
  main  默认 tag（无 tag 门面的日志归属）        框架预创建
  arm.joint   关节模块报文（场景 1/4 演示自定义 tag）   首次使用自动创建
  blackbox    安全黑匣子文件器（场景 3 创建/6 清档）    WARN/ERROR 转写落盘
  shiftlog    班次存档文件器（场景 4 创建/6 清档）      全级别存档落盘

运行方式：PYTHONPATH=src python3 examples/ExampleYomkLogger.py
"""

from pathlib import Path

import YomkApi
from YomkServerPy import Log
from YomkApi import log, log_mgr, server

# ---------------------------------------------------------------------------
# 辅助输出：全部走 YomkLogger 控制台日志（log()/log_mgr() 链式接口），无 print。
# 日志是最终面向用户的输出：每条自包含（谁+做什么+结果，关键数据随行）。
# ---------------------------------------------------------------------------

# 场景横幅：编号 + 标题 + 涉及接口，随后一行场景背景；横幅前留一个空行分隔场景
def log_scene(n, title, apis, explain):
    log().tag("arm.demo").info("")
    log().tag("arm.demo").info(f"========== 场景 {n}：{title}（{apis}）==========")
    log().tag("arm.demo").info("场景背景：" + explain)

# API 结果行：打印 YomkResponse 三要素中的 status/msg（数据形态各异，由调用处按需展开）。
# 日志门面的结果行自带流向证据：代理消费 msg="console log proxy is success."、放行 msg="success."。
# status 三态：0=eOk 成功；1=eNo 名字不存在或被拒绝；-1=eInvalid 参数无效或未初始化
def log_resp(api, resp):
    if resp.status == YomkApi.ResStatus.eOk:
        note = "(eOk 成功)"
    elif resp.status == YomkApi.ResStatus.eNo:
        note = "(eNo 拒绝或不存在)"
    else:
        note = "(eInvalid 未初始化或参数无效)"
    log().tag("arm.demo").info(f'[{api}] status={resp.status.value}{note}, msg="{resp.msg}"')

# 操作日志（YomkResponse 版）：先打"开始执行 {操作}"，再真正调用，最后打结果行——
# 操作是用户视角的业务动作（一个接口调用，或多个接口调用组合成的完整动作），
# 结果行标注本次接口调用；被演示的日志行（控制台）夹在开始行与结果行之间，
# 归属一目了然，读者顺着日志读即可理解，无需回看代码
def log_action(op, api, call):
    log().tag("arm.demo").info("开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp

# 操作开始行（返回值需要定制展开的动作，如 global_state 族 / 文件回读）：
# 由调用处随后打结果行
def log_action_start(op):
    log().tag("arm.demo").info("开始执行 " + op)

# 自省结果展开：单个日志器 LoggerInfo 的字段逐行展示（名称行 + type/状态行），
# /YomkLogger/logger 与 /YomkLogger/all 共用；console 条目状态段单行承载五开关
def log_logger_info(api, info):
    log().tag("arm.demo").info(f"[{api}] - {info.logger_name}")
    log().tag("arm.demo").info(f"[{api}]     type: {info.type}")

    if info.type == "file":
        log().tag("arm.demo").info(f"[{api}]     dir: {info.dir}")
        return
    state = info.logger_state
    log().tag("arm.demo").info(f"[{api}]     state: debug={'on' if state.debug_on else 'off'}"
        f" info={'on' if state.info_on else 'off'}"
        f" warn={'on' if state.warn_on else 'off'}"
        f" error={'on' if state.error_on else 'off'}"
        f" proxy={'on' if state.proxy_on else 'off'}")

# 全局主闸状态行：/YomkLogger/console_global_state 的 LoggerState 五开关单行展示（两层归因用）
def log_global_state(api, state):
    log().tag("arm.demo").info(f"[{api}] 主闸: debug={'on' if state.debug_on else 'off'}"
        f" info={'on' if state.info_on else 'off'}"
        f" warn={'on' if state.warn_on else 'off'}"
        f" error={'on' if state.error_on else 'off'}"
        f" proxy={'on' if state.proxy_on else 'off'}")

# /YomkLogger/loggerS 清单行：日志器名列表逐行展示（" - " 无序列表风格）
def log_logger_line(api, logger_name):
    log().tag("arm.demo").info(f"[{api}] - {logger_name}")

# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    log().tag("arm.demo").info("[验证] " + text)

# 回读 .log 文件逐行输出（arm.demo tag）——眼见为实的落盘验证，零 print
def log_file_content(path):
    with open(path, encoding="utf-8") as log_file:
        for line in log_file:
            log().tag("arm.demo").info("| " + line.rstrip("\n"))

# ---------------------------------------------------------------------------
# 控制台日志代理（场景 3）：关键事件转写黑匣子文件后消费、DEBUG 噪声消化、INFO 放行。
# 框架每写一条控制台日志先过本回调：返回 False 日志被消费（不再默认输出），
# 返回 True 放行继续框架默认打印。回调在调用方线程同步执行，内部只走文件日志
# （文件路径不经过代理，无递归），不可再打控制台日志。
# ---------------------------------------------------------------------------
def blackbox_proxy(pkg: Log) -> bool:
    # WARN/ERROR 关键事件：预拼一行（原文 + tag）转写黑匣子，控制台不再输出
    if pkg.m_level == Log.eWarn or pkg.m_level == Log.eError:
        text = f"{pkg.m_log} (tag: {pkg.m_tag})"
        if pkg.m_level == Log.eWarn:
            log().to_sink("blackbox").warn(text)
        else:
            log().to_sink("blackbox").error(text)
        return False
    # DEBUG 噪声：直接消费丢弃
    if pkg.m_level == Log.eDebug:
        return False
    # 其余（INFO）：放行，继续框架默认输出
    return True

# ---------------------------------------------------------------------------
# 文件日志代理（场景 3）：拦截 DEBUG 不落盘，其余穿透走默认落盘；与控制台代理
# 相互独立。回调在调用方线程同步执行，内部不要再写文件日志（同一链路会重入）。
# ---------------------------------------------------------------------------
def file_debug_proxy(log_pkg) -> bool:
    # 仅拦 DEBUG：DEBUG 被拦不落盘，INFO/WARN/ERROR 穿透正常写缓冲
    return log_pkg.m_level != Log.eDebug

# ---------------------------------------------------------------------------
# 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
# 文件器流转：blackbox/shiftlog 场景 3/4 创建，场景 6 一并清档。
# ---------------------------------------------------------------------------

# 场景 1：班前点检报文分级（四级 + tag 分层）
def show_arm_log_levels():
    log_scene(
        1,
        "班前点检报文分级",
        "log().info/warn/error/debug（六级）+ tag 形态",
        "班前点检报文按紧迫度分级：DEBUG 轨迹详单、INFO 流程通报、WARN 隐患预警、"
        "ERROR 故障告警——输出格式 [时间] [级别] [tag] [行号] 内容，[行号] 由门面自动拼接定位调用处。")

    # 默认 tag：main（框架预创建，无 tag 门面的日志均归属它）
    log_action("记录轨迹详单（DEBUG）", "log().debug()",
               lambda: log().debug("插补周期 4ms，指令缓冲占用 12%"))
    log_action("记录流程通报（INFO）", "log().info()",
               lambda: log().info("班前点检通过：产线就绪，工单 W-1024 下发"))
    log_action("上报隐患预警（WARN）", "log().warn()",
               lambda: log().warn("减速器油温 68℃，接近 70℃ 预警线"))
    log_action("上报故障告警（ERROR）", "log().error()",
               lambda: log().error("j2 轴伺服过流，已触发保护区急停"))
    log_verify("四条日志级别字段各异、[行号] 即调用处：被演示的日志行夹在开始行与结果行之间，归属一目了然")

    # 自定义 tag：arm.joint（tag 兼容双填兼作日志器名，首次使用自动创建）
    log_action("关节模块记录到位通报（tag 发射形态）", "log().tag(arm.joint).info()",
               lambda: log().tag("arm.joint").info("j1 轴到位，定位偏差 0.02mm"))
    log_action("关节模块记录电流详单（tag 形态）", "log().tag(arm.joint).debug()",
               lambda: log().tag("arm.joint").debug("j1 轴电流 3.2A，力矩 12Nm"))
    log_action("关节模块上报温升预警（tag 形态）", "log().tag(arm.joint).warn()",
               lambda: log().tag("arm.joint").warn("j2 轴温升 15℃/min，建议降速"))
    log_action("关节模块上报通信故障（tag 形态）", "log().tag(arm.joint).error()",
               lambda: log().tag("arm.joint").error("j3 轴编码器通信超时 200ms"))
    log_verify("tag 随记录显示且兼作日志器名（首次使用自动创建）：arm.joint 报文与叙事 arm.demo 按分层阅读")

# 场景 2：产线静默模式（四级别逐级全关 -> 逐级恢复）
# 注：级别关闭期间 INFO 叙事同样被抑制——空窗由 WARN/ERROR 锚点桥接，这本身
# 就是"开关只抑制输出、调用始终成功"的现场演示
def show_arm_silent_mode():
    log_scene(
        2,
        "产线静默模式",
        "/YomkLogger/on·off_console_log_by_level（无参=主闸/具名=逐器） + console_global_state + logger",
        "夜间无人值守降噪：四级别逐级全关（调用仍返回 eOk，仅输出被抑制），"
        "空窗段无任何控制台输出；恢复后单查状态段复查四级别全 on。")

    # 先关 DEBUG 详单（最常用的降噪手段）
    log_action("关闭 DEBUG 详单（产线静默）", "console().level(Debug).disable()",
               lambda: log_mgr().console().level(Log.eDebug).disable())
    log_verify("随后的 DEBUG 调用（静默段）仍返回 eOk 但不再输出：级别开关只抑制输出、不影响调用成功")

    log().tag("arm.demo").warn("继续降噪：INFO/WARN/ERROR 也将逐级关闭，空窗期间操作照常执行")
    log().tag("arm.demo").error("最后一条可见日志：ERROR 关闭后全静默，恢复后汇报空窗详情")

    log_action("关闭 INFO 流程通报", "console().level(Info).disable()",
               lambda: log_mgr().console().level(Log.eInfo).disable())
    log_action("关闭 WARN 隐患预警", "console().level(Warn).disable()",
               lambda: log_mgr().console().level(Log.eWarn).disable())
    log_action("关闭 ERROR 故障告警", "console().level(Error).disable()",
               lambda: log_mgr().console().level(Log.eError).disable())

    # 全静默段：以下 4 条调用成功返回 eOk，控制台一行都不出现
    log().info("你看不到我：INFO 已关闭")
    log().warn("你看不到我：WARN 已关闭")
    log().error("你看不到我：ERROR 已关闭")
    log().debug("你看不到我：DEBUG 已关闭")

    # 恢复：ERROR/INFO 先裸调恢复（INFO 回归后叙事锚点可见），随后逐级恢复
    log_mgr().console().level(Log.eError).enable()
    log_mgr().console().level(Log.eInfo).enable()
    log().tag("arm.demo").info("静默解除：空窗期间已按序执行 OFF_INFO/WARN/ERROR 与 4 条演示调用，全部返回 eOk——恢复 WARN/DEBUG 后复查")
    log_action("恢复 WARN 隐患预警", "console().level(Warn).enable()",
               lambda: log_mgr().console().level(Log.eWarn).enable())
    log_action("恢复 DEBUG 详单", "console().level(Debug).enable()",
               lambda: log_mgr().console().level(Log.eDebug).enable())

    log_action_start("复查 main 开关状态")
    resp = log_mgr().console("main").get_info()
    log_resp("/YomkLogger/logger main(console)", resp)
    if resp.data is not None:
        log_logger_info("/YomkLogger/logger", resp.data)
    log_verify("四级别全 on（代理未安装 proxy=off）：无参 OFF/ON 作用于全局主闸，"
               "状态段报逐器门控层真值（本段未用具名开关，两者一致）")

    # 逐器静默：具名开关仅门控单个日志器，全局主闸不受影响（两层归因现场演示）
    log_action("逐器静默：仅关闭 arm.joint 的 INFO（具名关断）", "console(arm.joint).level(Info).disable()",
               lambda: log_mgr().console("arm.joint").level(Log.eInfo).disable())
    log_action("关节模块上报工位动态（INFO，arm.joint 已被具名关断）", "log().tag(arm.joint).info()",
               lambda: log().tag("arm.joint").info("工位 A 首件下线（本条仅 arm.joint 静默，你看不到）"))
    log().tag("arm.demo").info("arm.joint 已逐器静默：arm.demo 叙事与 main 输出不受影响（对照锚点）")

    log_action_start("复查全局主闸状态")
    resp = log_mgr().console().status()
    log_resp("/YomkLogger/console_global_state", resp)
    if resp.data is not None:
        log_global_state("/YomkLogger/console_global_state", resp.data)
    log_action_start("单查 arm.joint 条目")
    resp = log_mgr().console("arm.joint").get_info()
    log_resp("/YomkLogger/logger arm.joint(console)", resp)
    if resp.data is not None:
        log_logger_info("/YomkLogger/logger arm.joint", resp.data)
    log_verify("主闸 info=on 而条目 info=off——两层状态各自真实呈现：输出被关是逐器层所致，具名 ON 即恢复")
    log_action("逐器恢复：仅重开 arm.joint 的 INFO（具名恢复）", "console(arm.joint).level(Info).enable()",
               lambda: log_mgr().console("arm.joint").level(Log.eInfo).enable())
    log_action("关节模块恢复上报（INFO，输出回归）", "log().tag(arm.joint).info()",
               lambda: log().tag("arm.joint").info("arm.joint INFO 已恢复，报文重新可见"))

# 场景 3：安全黑匣子（控制台日志代理：消费与放行）
def show_arm_blackbox(log_dir):
    log_scene(
        3,
        "安全黑匣子",
        "/YomkLogger/set_console_log_proxy / set_file_log_proxy / create_file_logger / write_file_log",
        "关键事件不能只留控制台：安装代理后每条控制台日志先过回调——WARN/ERROR "
        "转写黑匣子文件后消费、DEBUG 噪声就地消化、INFO 放行；结果行 msg 区分消费与放行；"
        "随后文件代理拦 DEBUG 不落盘、None 卸载恢复写入。")

    log_action("创建安全黑匣子文件器 blackbox", "file(blackbox).create()",
               lambda: log_mgr().dir(log_dir).file("blackbox").create())
    log_action("安装控制台日志代理", "console().proxy(blackbox_proxy).apply()",
               lambda: log_mgr().console().proxy(blackbox_proxy).apply())

    log_action("上报班中通报（INFO，代理放行）", "log().info()",
               lambda: log().info("工位 A 完成工单 W-1024 首件下线"))
    log_action("记录调试详单（DEBUG，代理消费）", "log().debug()",
               lambda: log().debug("视觉对位耗时 320ms，阈值 500ms"))
    log_action("上报隐患预警（WARN，代理转写黑匣子）", "log().warn()",
               lambda: log().warn("工位 B 气压 0.52MPa，低于额定 0.6MPa"))
    log_action("上报故障告警（ERROR，代理转写黑匣子）", "log().error()",
               lambda: log().error("传送带电机堵转，工位 C 停机"))
    log_verify(
        "结果行 msg 自证去向：放行 msg=success.、消费 msg=console log proxy is success.——"
        "4 条演示中仅 INFO 出现在控制台，WARN/ERROR 已转写黑匣子")

    log_action_start("查看代理安装状态")
    resp = log_mgr().console("main").get_info()
    log_resp("/YomkLogger/logger main(console)", resp)
    if resp.data is not None:
        log_logger_info("/YomkLogger/logger", resp.data)
    log_verify("state 段 proxy=on：代理是框架全局单点，接管全部控制台日志")

    log_action("刷盘安全黑匣子", "file(blackbox).flush()",
               lambda: log_mgr().file("blackbox").flush())
    log_action_start("回读黑匣子文件验证转写")
    log().tag("arm.demo").info("---- blackbox.log 内容 ----")
    log_file_content(Path(log_dir) / "blackbox.log")
    log_verify("blackbox.log 仅 2 条关键事件（WARN/ERROR）：黑匣子只留关键事件，噪声就地消化")

    log_action("卸载代理恢复默认输出", "console().proxy(None).apply()",
               lambda: log_mgr().console().proxy(None).apply())
    log_action_start("复查代理状态")
    resp = log_mgr().console("main").get_info()
    log_resp("/YomkLogger/logger main(console)", resp)
    if resp.data is not None:
        log_logger_info("/YomkLogger/logger", resp.data)
    log_verify("proxy 回到 off：传 None 即卸载，控制台恢复框架默认输出格式")

    # 文件日志代理段：与控制台代理相互独立，拦 DEBUG 不落盘、None 卸载恢复写入
    log_action("安装文件日志代理", "file().proxy(file_debug_proxy).apply()",
               lambda: log_mgr().file().proxy(file_debug_proxy).apply())
    log_action_start("复查文件侧全局主闸状态（文件代理态随装随报）")
    resp = log_mgr().file().status()
    log_resp("/YomkLogger/file_global_state", resp)
    if resp.data is not None:
        log_global_state("/YomkLogger/file_global_state", resp.data)
    log_action("记录 DEBUG 详单（文件代理拦截）", "log().to_sink(blackbox).debug()",
               lambda: log().to_sink("blackbox").debug("视觉对位耗时 350ms，阈值 500ms"))
    log_action("卸载文件日志代理", "file().proxy(None).apply()",
               lambda: log_mgr().file().proxy(None).apply())
    log_action("卸载后记录 DEBUG 详单（穿透）", "log().to_sink(blackbox).debug()",
               lambda: log().to_sink("blackbox").debug("卸载代理后此行正常写入缓冲"))
    log_verify("proxy=on 时 DEBUG 被拦（msg=file log proxy is success.）、None 卸载后 "
               "DEBUG 恢复写入——文件代理与控制台代理相互独立")

# 场景 4：班次存档（文件日志：缓冲、刷盘、与控制台独立）
def show_arm_shift_log(log_dir):
    log_scene(
        4,
        "班次存档",
        "log().to_sink().info/warn/error/debug（六级 sink 形态）+ /YomkLogger/on·off_file_log_by_level",
        "全级别班次存档：to_sink 发射写入内存缓冲，file().flush() 显式刷盘到 dir/name.log；"
        "文件开关与控制台开关两套相互独立——控制台 DEBUG 全关期间文件 DEBUG 照常"
        "落盘，文件具名关断期间条目静默吞条（msg 仍 success.），落盘与否回读实证。")

    log_action("创建班次存档文件器 shiftlog", "file(shiftlog).create()",
               lambda: log_mgr().dir(log_dir).file("shiftlog").create())

    # 默认 tag 写入（内容前缀仅 [行号]）
    log_action("存档流程通报（INFO）", "log().to_sink(shiftlog).info()",
               lambda: log().to_sink("shiftlog").info("班次开始，工单 W-1024 就绪"))
    log_action("存档隐患预警（WARN）", "log().to_sink(shiftlog).warn()",
               lambda: log().to_sink("shiftlog").warn("工位 A 气压 0.55MPa，低于额定 0.6"))
    log_action("存档故障告警（ERROR）", "log().to_sink(shiftlog).error()",
               lambda: log().to_sink("shiftlog").error("工位 B 视觉检测连续 3 次超时"))
    log_action("存档节拍详单（DEBUG）", "log().to_sink(shiftlog).debug()",
               lambda: log().to_sink("shiftlog").debug("节拍 12.4s/件，缓存占用 31%"))

    # 自定义 tag 写入（内容前缀 [tag] [行号]，文件内区分模块）
    log_action("关节模块存档插补通报（tag 发射形态）", "log().to_sink(shiftlog).tag(arm.joint).info()",
               lambda: log().to_sink("shiftlog").tag("arm.joint").info("j1 轴完成 1200 次插补"))
    log_action("关节模块存档温升预警（tag 形态）", "log().to_sink(shiftlog).tag(arm.joint).warn()",
               lambda: log().to_sink("shiftlog").tag("arm.joint").warn("j2 轴温度 71℃，建议班后保养"))
    log_action("关节模块存档力矩超限告警（tag 形态）", "log().to_sink(shiftlog).tag(arm.joint).error()",
               lambda: log().to_sink("shiftlog").tag("arm.joint").error("j3 轴力矩超限 118%"))
    log_action("关节模块存档间隙补偿详单（tag 形态）", "log().to_sink(shiftlog).tag(arm.joint).debug()",
               lambda: log().to_sink("shiftlog").tag("arm.joint").debug("j4 轴反向间隙补偿 0.03mm"))

    # 交叉验证：控制台 DEBUG 全关期间，文件 DEBUG 照常落盘
    log_action("关闭 DEBUG 详单（交叉验证）", "console().level(Debug).disable()",
               lambda: log_mgr().console().level(Log.eDebug).disable())
    log_action("开关关闭期间存档详单（DEBUG）", "log().to_sink(shiftlog).debug()",
               lambda: log().to_sink("shiftlog").debug("交叉验证：控制台 DEBUG 已关，此行仍应落盘"))
    log_action("恢复 DEBUG 详单", "console().level(Debug).enable()",
               lambda: log_mgr().console().level(Log.eDebug).enable())

    # 存档静默：文件具名关断期间条目静默吞条（msg 仍 success.），缺席归因逐器门控层
    log_action("具名关断 shiftlog 的 INFO", "file(shiftlog).level(Info).disable()",
               lambda: log_mgr().file("shiftlog").level(Log.eInfo).disable())
    log_action("静默期间存档（INFO）", "log().to_sink(shiftlog).info()",
               lambda: log().to_sink("shiftlog").info("静默期间此行不应落盘"))
    log_action_start("复查文件侧全局主闸状态")
    resp = log_mgr().file().status()
    log_resp("/YomkLogger/file_global_state", resp)
    if resp.data is not None:
        log_global_state("/YomkLogger/file_global_state", resp.data)
    log_verify("文件主闸仍全 on：INFO 缺席归因逐器门控层，两层状态各自真实呈现")
    log_action("具名恢复 shiftlog 的 INFO", "file(shiftlog).level(Info).enable()",
               lambda: log_mgr().file("shiftlog").level(Log.eInfo).enable())

    log_action("显式刷盘班次存档", "file(shiftlog).flush()",
               lambda: log_mgr().file("shiftlog").flush())
    log_action_start("回读班次存档文件")
    log().tag("arm.demo").info("---- shiftlog.log 内容 ----")
    log_file_content(Path(log_dir) / "shiftlog.log")
    log_verify("9 条四级双 tag 落盘、静默段 INFO 缺席：文件开关只影响文件写入，"
               "与控制台开关相互独立（写缓冲在先、显式 WRITE 落盘在后）")

# 场景 5：日志系统运维面板（内省三件套 + 全局主闸态单查）
def show_arm_logger_dashboard():
    log_scene(
        5,
        "日志系统运维面板",
        "/YomkLogger/loggers / logger / all / console·file_global_state",
        "班中巡检日志系统：loggers 端点按类型拉取日志器名清单（字典序）；LOGGER 按类型"
        "单查结构化 LoggerInfo（两表允许同名，须精确指定）；all 端点全量导出——"
        "框架只回结构化数据，展示格式由用户定义。")

    log_action_start("巡检控制台日志器清单")
    console_resp = log_mgr().console().list()
    log_resp("/YomkLogger/loggerS console", console_resp)
    if console_resp.data:
        for logger_name in console_resp.data:
            log_logger_line("/YomkLogger/loggerS", logger_name)
    log_verify("main（预创建）与 arm.demo/arm.joint（tag 惰性创建）均在列：清单按名字字典序排列")

    log_action_start("巡检文件日志器清单")
    file_resp = log_mgr().file().list()
    log_resp("/YomkLogger/loggerS file", file_resp)
    if file_resp.data:
        for logger_name in file_resp.data:
            log_logger_line("/YomkLogger/loggerS", logger_name)
    log_verify("blackbox 与 shiftlog 在列：控制台与文件是两张独立的日志器表")

    log_action_start("单查班次存档器 shiftlog")
    resp = log_mgr().file("shiftlog").get_info()
    log_resp("/YomkLogger/logger shiftlog(file)", resp)
    if resp.data is not None:
        log_logger_info("/YomkLogger/logger", resp.data)
    log_verify('type=file、dir 承载日志目录：msg 恒 "ok"，数据走 data 结构化字段')

    log_action_start("单查未注册日志器 ghost")
    resp = log_mgr().console("ghost").get_info()
    log_resp("/YomkLogger/logger ghost(console)", resp)
    log_verify("未注册名字 -> eNo(logger not found.)：not-found 是框架统一惯例")

    log_action_start("全量导出日志器状态")
    resp = log_mgr().all()
    log_resp("/YomkLogger/all", resp)
    if resp.data:
        for info in resp.data:
            log_logger_info("/YomkLogger/all", info)
    log_verify(
        "console/file 条目各自报逐器门控层真值（未做具名开关恒全 on）与对应侧代理态——两层归因的条目侧")

    # 全局主闸态单查：ALL 条目报逐器真值，主闸态经 console·file global_state 端点独立承载，两层一起看全
    log_action_start("复查控制台侧全局主闸状态")
    resp = log_mgr().console().status()
    log_resp("/YomkLogger/console_global_state", resp)
    if resp.data is not None:
        log_global_state("/YomkLogger/console_global_state", resp.data)
    log_action_start("复查文件侧全局主闸状态")
    resp = log_mgr().file().status()
    log_resp("/YomkLogger/file_global_state", resp)
    if resp.data is not None:
        log_global_state("/YomkLogger/file_global_state", resp.data)
    log_verify("条目逐器真值 + 两侧主闸态独立单查：两套主闸/两套代理各自真实呈现，"
               "输出与落盘归因唯一（条目 off 是逐器层所致、条目 on 而无输出查对应侧主闸）")

# 场景 6：收班清档（DELETE 两表清理、磁盘保留、惰性重建）
def show_arm_log_cleanup(log_dir):
    log_scene(
        6,
        "收班清档",
        "/YomkLogger/delete_logger + 惰性重建",
        "收班下线文件器与默认日志器：delete_logger 端点按名同时清理 console+file 两表并返回"
        "各清了几条，磁盘 .log 文件保留（数据保全归调用方）；删除后再写自动重建。")

    log_action("清档安全黑匣子 blackbox", "file(blackbox).remove()",
               lambda: log_mgr().file("blackbox").remove())
    log_action("清档班次存档 shiftlog", "file(shiftlog).remove()",
               lambda: log_mgr().file("shiftlog").remove())
    log_verify("msg=deleted console:0 file:1：blackbox 只在 file 表，delete_logger 端点报告两表各自清理数")

    log_action("清档未注册日志器 ghost", "file(ghost).remove()",
               lambda: log_mgr().file("ghost").remove())
    log_verify("两表均未命中 -> eNo(logger not found.)：not-found 是框架统一惯例")

    log_action("清档默认日志器 main", "file(main).remove()",
               lambda: log_mgr().file("main").remove())
    log_verify("msg=deleted console:1 file:0：main 只在 console 表（框架预创建）")

    log_action_start("回读班次存档文件确认保留")
    log().tag("arm.demo").info("---- shiftlog.log 内容（日志器已删、磁盘仍在） ----")
    log_file_content(Path(log_dir) / "shiftlog.log")
    log_verify("DELETE 只清内存表：磁盘 .log 文件保留，数据保全归调用方")

    log_action("再写默认日志（验证惰性重建）", "log().info()",
               lambda: log().info("main recreated on demand"))
    log_action_start("复查控制台清单确认重建")
    resp = log_mgr().console().list()
    log_resp("/YomkLogger/loggerS console", resp)
    if resp.data:
        for logger_name in resp.data:
            log_logger_line("/YomkLogger/loggerS", logger_name)
    log_verify("main 回归清单：日志器按名惰性创建，无需手动注册；班次结束，arm.demo 叙事仍在")

def main():
    # 初始化框架（内置日志服务 /YomkLogger 随之自动启动，main 预创建）
    server().init()

    log().tag("arm.demo").info("机械臂产线 x YomkLogger 示例：一次班次贯穿 6 个场景，覆盖全部日志 API")
    log().tag("arm.demo").info("日志按 tag 分层：arm.demo=叙事与结果 / main=默认日志器 / "
        "arm.joint=关节模块（演示自定义 tag）/ blackbox、shiftlog=文件器")

    # 日志目录：当前工作目录下的 logs/（场景 3/4 创建文件日志器使用，目录由
    # 框架自动创建）。不解析启动脚本路径——直接用 cwd 即可在启动控制台所在目录落盘
    log_dir = str(Path.cwd() / "logs")

    show_arm_log_levels()
    show_arm_silent_mode()
    show_arm_blackbox(log_dir)
    show_arm_shift_log(log_dir)
    show_arm_logger_dashboard()
    show_arm_log_cleanup(log_dir)

    log().tag("arm.demo").info("========== 示例结束：按回车退出 ==========")
    input()

if __name__ == "__main__":
    main()
