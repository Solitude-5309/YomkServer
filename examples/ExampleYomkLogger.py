#!/usr/bin/env python3
"""
ExampleYomkLogger —— YomkLogger 日志系统示例：机械臂产线背景的 6 个场景小示例（面向初学者）

日志系统提供开箱即用的双路输出：控制台日志（四级级别 + tag 分层 + 代理拦截）
与文件日志（内存缓冲 + 显式刷盘），并附带日志器内省。随 YomkApi.init() 自动启动。
机械臂产线中运行足迹走控制台（班前点检、班中预警、班后清档），关键事件另存
黑匣子与班次存档（文件日志），运维班前班后用内看点检日志系统本身。

控制台输出格式四段：[时间] [级别] [tag] [行号] 内容——tag 即控制台日志器名，
首次使用自动创建；门面自动把 [行号] 拼进内容开头，方便定位调用处。
级别开关两级主闸并联：无参作用于全局主闸（框架全局）、具名逐器门控单个日志器，
输出取两层 AND；控制台与文件两套主闸相互独立（on/off_file_log_* 操作文件侧）。

本示例用"机械臂产线一次班次"贯穿 6 个场景：每个场景是一个 show_arm_xxx
小函数，形成独立小闭环（背景铺垫 -> 操作 -> 日志自证），允许组合多个接口；
全部日志用户门面 API（控制台 8 / 开关 16 / 代理 1 / 文件 10 / 内省 4）无一遗漏；
开关含控制台与文件两族各 8（本示例演示控制台族，文件族 on/off_file_log_* 同构）。
叙事日志统一走 arm.demo（YomkLogger 自身即演示对象）。

小示例清单（场景 / 函数 / 展示的接口 / 设计思路）：

场景 1 show_arm_log_levels   [console_log_info/warn/error/debug + _tag 版]
       班前点检报文分级：四级日志按 DEBUG 详单 -> INFO 通报 -> WARN 预警 ->
       ERROR 告警递进；默认 tag 与自定义 tag 双填兼容，[行号] 随行定位。
场景 2 show_arm_silent_mode  [off/on_console_log_*（无参/具名）+ logger_info_global + logger_info_logger]
       产线静默模式：全局静默四级别逐级全关（空窗由 WARN/ERROR 锚点桥接，调用
       仍返回 eOk）；逐级恢复后单查 MainLogger 状态段复查四级别全 on；随后
       逐器静默——具名关断 arm.joint 仅其报文消失（arm.demo 对照不受影响），
       logger_info_global 报主闸全 on 与条目 info_off 两层归因唯一。
场景 3 show_arm_blackbox     [set_console_log_proxy / file_log_create / file_log_write]
       安全黑匣子：代理把 WARN/ERROR 转写文件后消费、DEBUG 噪声消化、INFO
       放行——结果行 msg 区分消费与放行；传 None 卸载恢复默认。
场景 4 show_arm_shift_log    [file_log_info/warn/error/debug + _tag 版]
       班次存档：双 tag 四级写入内存缓冲，显式 WRITE 刷盘；级别全关期间
       DEBUG 照常落盘——文件日志与控制台开关、代理相互独立。
场景 5 show_arm_logger_dashboard [logger_info_loggers / logger_info_logger / logger_info_all / logger_info_global]
       运维面板：console/file 清单（字典序）、单查结构化 LoggerInfo（含未
       注册 eNo）、全量导出——console 条目承载逐器门控层真值，再查
       logger_info_global 复核全局主闸态，两层一起看全。
场景 6 show_arm_log_cleanup  [file_log_delete + 惰性重建]
       收班清档：DELETE 同名清 console+file 两表（返回清了几张表）、磁盘
       .log 保留；MainLogger 删除后再写自动重建（按名惰性创建）。

tag 规划（arm.demo 叙事之外，演示对象按业务分层）：
  MainLogger  默认 tag（无 tag 门面的日志归属）        框架预创建
  arm.joint   关节模块报文（场景 1/4 演示自定义 tag）   首次使用自动创建
  blackbox    安全黑匣子文件器（场景 3 创建/6 清档）    WARN/ERROR 转写落盘
  shiftlog    班次存档文件器（场景 4 创建/6 清档）      全级别存档落盘

运行方式：PYTHONPATH=src python3 examples/ExampleYomkLogger.py
"""

from pathlib import Path

import YomkApi
from YomkServerPy import Log


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
# 日志门面的结果行自带流向证据：代理消费 msg="console log proxy is success."、放行 msg="success."。
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
# 结果行标注本次接口调用；被演示的日志行（控制台）夹在开始行与结果行之间，
# 归属一目了然，读者顺着日志读即可理解，无需回看代码
def log_action(op, api, call):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)
    resp = call()
    log_resp(api, resp)
    return resp


# 操作开始行（返回值需要定制展开的动作，如 LOGGER_INFO_* / 文件回读）：
# 由调用处随后打结果行
def log_action_start(op):
    YomkApi.console_log_info_tag("arm.demo", "开始执行 " + op)


# 自省结果展开：单个日志器 LoggerInfo 的字段逐行展示（名称行 + type/状态行），
# LOGGER_INFO_LOGGER 与 LOGGER_INFO_ALL 共用；console 条目状态段单行承载五开关
def log_logger_info(api, info):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {info.logger_name}")
    YomkApi.console_log_info_tag("arm.demo", f"[{api}]     type: {info.type}")

    if info.type == "file":
        YomkApi.console_log_info_tag(
            "arm.demo", f"[{api}]     dir: {info.dir}")
        return
    state = info.logger_state
    YomkApi.console_log_info_tag(
        "arm.demo",
        f"[{api}]     state: debug={'on' if state.debug_on else 'off'}"
        f" info={'on' if state.info_on else 'off'}"
        f" warn={'on' if state.warn_on else 'off'}"
        f" error={'on' if state.error_on else 'off'}"
        f" proxy={'on' if state.proxy_on else 'off'}")


# 全局主闸状态行：LOGGER_INFO_GLOBAL 的 LoggerState 五开关单行展示（两层归因用）
def log_global_state(api, state):
    YomkApi.console_log_info_tag(
        "arm.demo",
        f"[{api}] 主闸: debug={'on' if state.debug_on else 'off'}"
        f" info={'on' if state.info_on else 'off'}"
        f" warn={'on' if state.warn_on else 'off'}"
        f" error={'on' if state.error_on else 'off'}"
        f" proxy={'on' if state.proxy_on else 'off'}")


# LOGGER_INFO_LOGGERS 清单行：日志器名列表逐行展示（" - " 无序列表风格）
def log_logger_line(api, logger_name):
    YomkApi.console_log_info_tag("arm.demo", f"[{api}] - {logger_name}")


# 自证结论：期望与实际写在同一行，读者无需查源码即可确认框架行为
def log_verify(text):
    YomkApi.console_log_info_tag("arm.demo", "[验证] " + text)


# 回读 .log 文件逐行输出（arm.demo tag）——眼见为实的落盘验证，零 print
def log_file_content(path):
    with open(path, encoding="utf-8") as log_file:
        for line in log_file:
            YomkApi.console_log_info_tag("arm.demo", "| " + line.rstrip("\n"))


# ---------------------------------------------------------------------------
# 控制台日志代理（场景 3）：关键事件转写黑匣子文件后消费、DEBUG 噪声消化、INFO 放行。
# 框架每写一条控制台日志先过本回调：返回 False 日志被消费（不再默认输出），
# 返回 True 放行继续框架默认打印。回调在调用方线程同步执行，内部只走文件日志
# （文件路径不经过代理，无递归），不可再打控制台日志。
# ---------------------------------------------------------------------------
def blackbox_proxy(log: Log) -> bool:
    # WARN/ERROR 关键事件：预拼一行（原文 + tag）转写黑匣子，控制台不再输出
    if log.m_level == Log.eWarn or log.m_level == Log.eError:
        text = f"{log.m_log} (tag: {log.m_tag})"
        if log.m_level == Log.eWarn:
            YomkApi.file_log_warn("blackbox", text)
        else:
            YomkApi.file_log_error("blackbox", text)
        return False
    # DEBUG 噪声：直接消费丢弃
    if log.m_level == Log.eDebug:
        return False
    # 其余（INFO）：放行，继续框架默认输出
    return True


# ---------------------------------------------------------------------------
# 6 个场景小示例：每个函数独立小闭环，可单独阅读；main 按产线班次顺序编排。
# 文件器流转：blackbox/shiftlog 场景 3/4 创建，场景 6 一并清档。
# ---------------------------------------------------------------------------

# 场景 1：班前点检报文分级（四级 + tag 分层）
def show_arm_log_levels():
    log_scene(
        1,
        "班前点检报文分级",
        "console_log_info/warn/error/debug + _tag 版",
        "班前点检报文按紧迫度分级：DEBUG 轨迹详单、INFO 流程通报、WARN 隐患预警、"
        "ERROR 故障告警——输出格式 [时间] [级别] [tag] [行号] 内容，[行号] 由门面自动拼接定位调用处。")

    # 默认 tag：MainLogger（框架预创建，无 tag 门面的日志均归属它）
    log_action("记录轨迹详单（DEBUG）", "CONSOLE_LOG_DEBUG",
               lambda: YomkApi.console_log_debug("插补周期 4ms，指令缓冲占用 12%"))
    log_action("记录流程通报（INFO）", "CONSOLE_LOG_INFO",
               lambda: YomkApi.console_log_info("班前点检通过：产线就绪，工单 W-1024 下发"))
    log_action("上报隐患预警（WARN）", "CONSOLE_LOG_WARN",
               lambda: YomkApi.console_log_warn("减速器油温 68℃，接近 70℃ 预警线"))
    log_action("上报故障告警（ERROR）", "CONSOLE_LOG_ERROR",
               lambda: YomkApi.console_log_error("j2 轴伺服过流，已触发保护区急停"))
    log_verify("四条日志级别字段各异、[行号] 即调用处：被演示的日志行夹在开始行与结果行之间，归属一目了然")

    # 自定义 tag：arm.joint（tag 兼容双填兼作日志器名，首次使用自动创建）
    log_action("关节模块记录到位通报（INFO_TAG）", "CONSOLE_LOG_INFO_TAG arm.joint",
               lambda: YomkApi.console_log_info_tag("arm.joint", "j1 轴到位，定位偏差 0.02mm"))
    log_action("关节模块记录电流详单（DEBUG_TAG）", "CONSOLE_LOG_DEBUG_TAG arm.joint",
               lambda: YomkApi.console_log_debug_tag("arm.joint", "j1 轴电流 3.2A，力矩 12Nm"))
    log_action("关节模块上报温升预警（WARN_TAG）", "CONSOLE_LOG_WARN_TAG arm.joint",
               lambda: YomkApi.console_log_warn_tag("arm.joint", "j2 轴温升 15℃/min，建议降速"))
    log_action("关节模块上报通信故障（ERROR_TAG）", "CONSOLE_LOG_ERROR_TAG arm.joint",
               lambda: YomkApi.console_log_error_tag("arm.joint", "j3 轴编码器通信超时 200ms"))
    log_verify("tag 随记录显示且兼作日志器名（首次使用自动创建）：arm.joint 报文与叙事 arm.demo 按分层阅读")


# 场景 2：产线静默模式（四级别逐级全关 -> 逐级恢复）
# 注：级别关闭期间 INFO 叙事同样被抑制——空窗由 WARN/ERROR 锚点桥接，这本身
# 就是"开关只抑制输出、调用始终成功"的现场演示
def show_arm_silent_mode():
    log_scene(
        2,
        "产线静默模式",
        "off/on_console_log_*（无参/具名） + logger_info_global + logger_info_logger",
        "夜间无人值守降噪：四级别逐级全关（调用仍返回 eOk，仅输出被抑制），"
        "空窗段无任何控制台输出；恢复后单查状态段复查四级别全 on。")

    # 先关 DEBUG 详单（最常用的降噪手段）
    log_action("关闭 DEBUG 详单（产线静默）", "OFF_CONSOLE_LOG_DEBUG",
               lambda: YomkApi.off_console_log_debug())
    log_verify("随后的 DEBUG 调用（静默段）仍返回 eOk 但不再输出：级别开关只抑制输出、不影响调用成功")

    YomkApi.console_log_warn_tag(
        "arm.demo", "继续降噪：INFO/WARN/ERROR 也将逐级关闭，空窗期间操作照常执行")
    YomkApi.console_log_error_tag(
        "arm.demo", "最后一条可见日志：ERROR 关闭后全静默，恢复后汇报空窗详情")

    log_action("关闭 INFO 流程通报", "OFF_CONSOLE_LOG_INFO",
               lambda: YomkApi.off_console_log_info())
    log_action("关闭 WARN 隐患预警", "OFF_CONSOLE_LOG_WARN",
               lambda: YomkApi.off_console_log_warn())
    log_action("关闭 ERROR 故障告警", "OFF_CONSOLE_LOG_ERROR",
               lambda: YomkApi.off_console_log_error())

    # 全静默段：以下 4 条调用成功返回 eOk，控制台一行都不出现
    YomkApi.console_log_info("你看不到我：INFO 已关闭")
    YomkApi.console_log_warn("你看不到我：WARN 已关闭")
    YomkApi.console_log_error("你看不到我：ERROR 已关闭")
    YomkApi.console_log_debug("你看不到我：DEBUG 已关闭")

    # 恢复：ERROR/INFO 先裸调恢复（INFO 回归后叙事锚点可见），随后逐级恢复
    YomkApi.on_console_log_error()
    YomkApi.on_console_log_info()
    YomkApi.console_log_info_tag(
        "arm.demo",
        "静默解除：空窗期间已按序执行 OFF_INFO/WARN/ERROR 与 4 条演示调用，全部返回 eOk——恢复 WARN/DEBUG 后复查")
    log_action("恢复 WARN 隐患预警", "ON_CONSOLE_LOG_WARN",
               lambda: YomkApi.on_console_log_warn())
    log_action("恢复 DEBUG 详单", "ON_CONSOLE_LOG_DEBUG",
               lambda: YomkApi.on_console_log_debug())

    log_action_start("复查 MainLogger 开关状态")
    resp = YomkApi.logger_info_logger("MainLogger", "console")
    log_resp("LOGGER_INFO_LOGGER MainLogger(console)", resp)
    if resp.data is not None:
        log_logger_info("LOGGER_INFO_LOGGER", resp.data)
    log_verify("四级别全 on（代理未安装 proxy=off）：无参 OFF/ON 作用于全局主闸，"
               "状态段报逐器门控层真值（本段未用具名开关，两者一致）")

    # 逐器静默：具名开关仅门控单个日志器，全局主闸不受影响（两层归因现场演示）
    log_action("逐器静默：仅关闭 arm.joint 的 INFO（具名关断）", "OFF_CONSOLE_LOG_INFO arm.joint",
               lambda: YomkApi.off_console_log_info("arm.joint"))
    log_action("关节模块上报工位动态（INFO，arm.joint 已被具名关断）", "CONSOLE_LOG_INFO_TAG arm.joint",
               lambda: YomkApi.console_log_info_tag(
                   "arm.joint", "工位 A 首件下线（本条仅 arm.joint 静默，你看不到）"))
    YomkApi.console_log_info_tag(
        "arm.demo", "arm.joint 已逐器静默：arm.demo 叙事与 MainLogger 输出不受影响（对照锚点）")

    log_action_start("复查全局主闸状态")
    resp = YomkApi.logger_info_global()
    log_resp("LOGGER_INFO_GLOBAL", resp)
    if resp.data is not None:
        log_global_state("LOGGER_INFO_GLOBAL", resp.data)
    log_action_start("单查 arm.joint 条目")
    resp = YomkApi.logger_info_logger("arm.joint", "console")
    log_resp("LOGGER_INFO_LOGGER arm.joint(console)", resp)
    if resp.data is not None:
        log_logger_info("LOGGER_INFO_LOGGER arm.joint", resp.data)
    log_verify("主闸 info=on 而条目 info=off——两层状态各自真实呈现：输出被关是逐器层所致，具名 ON 即恢复")
    log_action("逐器恢复：仅重开 arm.joint 的 INFO（具名恢复）", "ON_CONSOLE_LOG_INFO arm.joint",
               lambda: YomkApi.on_console_log_info("arm.joint"))
    log_action("关节模块恢复上报（INFO，输出回归）", "CONSOLE_LOG_INFO_TAG arm.joint",
               lambda: YomkApi.console_log_info_tag("arm.joint", "arm.joint INFO 已恢复，报文重新可见"))


# 场景 3：安全黑匣子（控制台日志代理：消费与放行）
def show_arm_blackbox(log_dir):
    log_scene(
        3,
        "安全黑匣子",
        "set_console_log_proxy / file_log_create / file_log_write",
        "关键事件不能只留控制台：安装代理后每条控制台日志先过回调——WARN/ERROR "
        "转写黑匣子文件后消费、DEBUG 噪声就地消化、INFO 放行；结果行 msg 区分消费与放行。")

    log_action("创建安全黑匣子文件器 blackbox", "FILE_LOG_CREATE blackbox",
               lambda: YomkApi.file_log_create(log_dir, "blackbox"))
    log_action("安装控制台日志代理", "SET_CONSOLE_LOG_PROXY blackbox_proxy",
               lambda: YomkApi.set_console_log_proxy(blackbox_proxy))

    log_action("上报班中通报（INFO，代理放行）", "CONSOLE_LOG_INFO",
               lambda: YomkApi.console_log_info("工位 A 完成工单 W-1024 首件下线"))
    log_action("记录调试详单（DEBUG，代理消费）", "CONSOLE_LOG_DEBUG",
               lambda: YomkApi.console_log_debug("视觉对位耗时 320ms，阈值 500ms"))
    log_action("上报隐患预警（WARN，代理转写黑匣子）", "CONSOLE_LOG_WARN",
               lambda: YomkApi.console_log_warn("工位 B 气压 0.52MPa，低于额定 0.6MPa"))
    log_action("上报故障告警（ERROR，代理转写黑匣子）", "CONSOLE_LOG_ERROR",
               lambda: YomkApi.console_log_error("传送带电机堵转，工位 C 停机"))
    log_verify(
        "结果行 msg 自证去向：放行 msg=success.、消费 msg=console log proxy is success.——"
        "4 条演示中仅 INFO 出现在控制台，WARN/ERROR 已转写黑匣子")

    log_action_start("查看代理安装状态")
    resp = YomkApi.logger_info_logger("MainLogger", "console")
    log_resp("LOGGER_INFO_LOGGER MainLogger(console)", resp)
    if resp.data is not None:
        log_logger_info("LOGGER_INFO_LOGGER", resp.data)
    log_verify("state 段 proxy=on：代理是框架全局单点，接管全部控制台日志")

    log_action("刷盘安全黑匣子", "FILE_LOG_WRITE blackbox",
               lambda: YomkApi.file_log_write("blackbox"))
    log_action_start("回读黑匣子文件验证转写")
    YomkApi.console_log_info_tag("arm.demo", "---- blackbox.log 内容 ----")
    log_file_content(Path(log_dir) / "blackbox.log")
    log_verify("blackbox.log 仅 2 条关键事件（WARN/ERROR）：黑匣子只留关键事件，噪声就地消化")

    log_action("卸载代理恢复默认输出", "SET_CONSOLE_LOG_PROXY None",
               lambda: YomkApi.set_console_log_proxy(None))
    log_action_start("复查代理状态")
    resp = YomkApi.logger_info_logger("MainLogger", "console")
    log_resp("LOGGER_INFO_LOGGER MainLogger(console)", resp)
    if resp.data is not None:
        log_logger_info("LOGGER_INFO_LOGGER", resp.data)
    log_verify("proxy 回到 off：传 None 即卸载，控制台恢复框架默认输出格式")


# 场景 4：班次存档（文件日志：缓冲、刷盘、与控制台独立）
def show_arm_shift_log(log_dir):
    log_scene(
        4,
        "班次存档",
        "file_log_info/warn/error/debug + _tag 版",
        "全级别班次存档：FILE 系列门面写入内存缓冲，WRITE 显式刷盘到 dir/name.log；"
        "文件日志不经控制台级别开关与代理，两路输出相互独立。")

    log_action("创建班次存档文件器 shiftlog", "FILE_LOG_CREATE shiftlog",
               lambda: YomkApi.file_log_create(log_dir, "shiftlog"))

    # 默认 tag 写入（内容前缀仅 [行号]）
    log_action("存档流程通报（INFO）", "FILE_LOG_INFO shiftlog",
               lambda: YomkApi.file_log_info("shiftlog", "班次开始，工单 W-1024 就绪"))
    log_action("存档隐患预警（WARN）", "FILE_LOG_WARN shiftlog",
               lambda: YomkApi.file_log_warn("shiftlog", "工位 A 气压 0.55MPa，低于额定 0.6"))
    log_action("存档故障告警（ERROR）", "FILE_LOG_ERROR shiftlog",
               lambda: YomkApi.file_log_error("shiftlog", "工位 B 视觉检测连续 3 次超时"))
    log_action("存档节拍详单（DEBUG）", "FILE_LOG_DEBUG shiftlog",
               lambda: YomkApi.file_log_debug("shiftlog", "节拍 12.4s/件，缓存占用 31%"))

    # 自定义 tag 写入（内容前缀 [tag] [行号]，文件内区分模块）
    log_action("关节模块存档插补通报（INFO_TAG）", "FILE_LOG_INFO_TAG shiftlog arm.joint",
               lambda: YomkApi.file_log_info_tag("shiftlog", "arm.joint", "j1 轴完成 1200 次插补"))
    log_action("关节模块存档温升预警（WARN_TAG）", "FILE_LOG_WARN_TAG shiftlog arm.joint",
               lambda: YomkApi.file_log_warn_tag("shiftlog", "arm.joint", "j2 轴温度 71℃，建议班后保养"))
    log_action("关节模块存档力矩超限告警（ERROR_TAG）", "FILE_LOG_ERROR_TAG shiftlog arm.joint",
               lambda: YomkApi.file_log_error_tag("shiftlog", "arm.joint", "j3 轴力矩超限 118%"))
    log_action("关节模块存档间隙补偿详单（DEBUG_TAG）", "FILE_LOG_DEBUG_TAG shiftlog arm.joint",
               lambda: YomkApi.file_log_debug_tag("shiftlog", "arm.joint", "j4 轴反向间隙补偿 0.03mm"))

    # 交叉验证：控制台 DEBUG 全关期间，文件 DEBUG 照常落盘
    log_action("关闭 DEBUG 详单（交叉验证）", "OFF_CONSOLE_LOG_DEBUG",
               lambda: YomkApi.off_console_log_debug())
    log_action("开关关闭期间存档详单（DEBUG）", "FILE_LOG_DEBUG shiftlog",
               lambda: YomkApi.file_log_debug("shiftlog", "交叉验证：控制台 DEBUG 已关，此行仍应落盘"))
    log_action("恢复 DEBUG 详单", "ON_CONSOLE_LOG_DEBUG",
               lambda: YomkApi.on_console_log_debug())

    log_action("显式刷盘班次存档", "FILE_LOG_WRITE shiftlog",
               lambda: YomkApi.file_log_write("shiftlog"))
    log_action_start("回读班次存档文件")
    YomkApi.console_log_info_tag("arm.demo", "---- shiftlog.log 内容 ----")
    log_file_content(Path(log_dir) / "shiftlog.log")
    log_verify("9 条四级双 tag 全部落盘：FILE 写入不经级别开关与代理（写缓冲在先、显式 WRITE 落盘在后）")


# 场景 5：日志系统运维面板（内省三件套 + 全局主闸态单查）
def show_arm_logger_dashboard():
    log_scene(
        5,
        "日志系统运维面板",
        "logger_info_loggers / logger_info_logger / logger_info_all / logger_info_global",
        "班中巡检日志系统：LOGGERS 按类型拉取日志器名清单（字典序）；LOGGER 按类型"
        "单查结构化 LoggerInfo（两表允许同名，须精确指定）；ALL 全量导出——"
        "框架只回结构化数据，展示格式由用户定义。")

    log_action_start("巡检控制台日志器清单")
    console_resp = YomkApi.logger_info_loggers("console")
    log_resp("LOGGER_INFO_LOGGERS console", console_resp)
    if console_resp.data:
        for logger_name in console_resp.data:
            log_logger_line("LOGGER_INFO_LOGGERS", logger_name)
    log_verify("MainLogger（预创建）与 arm.demo/arm.joint（tag 惰性创建）均在列：清单按名字字典序排列")

    log_action_start("巡检文件日志器清单")
    file_resp = YomkApi.logger_info_loggers("file")
    log_resp("LOGGER_INFO_LOGGERS file", file_resp)
    if file_resp.data:
        for logger_name in file_resp.data:
            log_logger_line("LOGGER_INFO_LOGGERS", logger_name)
    log_verify("blackbox 与 shiftlog 在列：控制台与文件是两张独立的日志器表")

    log_action_start("单查班次存档器 shiftlog")
    resp = YomkApi.logger_info_logger("shiftlog", "file")
    log_resp("LOGGER_INFO_LOGGER shiftlog(file)", resp)
    if resp.data is not None:
        log_logger_info("LOGGER_INFO_LOGGER", resp.data)
    log_verify('type=file、dir 承载日志目录：msg 恒 "ok"，数据走 data 结构化字段')

    log_action_start("单查未注册日志器 ghost")
    resp = YomkApi.logger_info_logger("ghost", "console")
    log_resp("LOGGER_INFO_LOGGER ghost(console)", resp)
    log_verify("未注册名字 -> eNo(logger not found.)：not-found 是框架统一惯例")

    log_action_start("全量导出日志器状态")
    resp = YomkApi.logger_info_all()
    log_resp("LOGGER_INFO_ALL", resp)
    if resp.data:
        for info in resp.data:
            log_logger_info("LOGGER_INFO_ALL", info)
    log_verify(
        "console 条目 state 为控制台逐器门控层真值（未做具名开关恒全 on），file 条目为文件逐器门控层真值、proxy 恒 off（文件代理落地前）")

    # 全局主闸态单查：ALL 条目报逐器真值，主闸态经 logger_info_global 独立承载，两层一起看全
    log_action_start("复查全局主闸状态")
    resp = YomkApi.logger_info_global()
    log_resp("LOGGER_INFO_GLOBAL", resp)
    if resp.data is not None:
        log_global_state("LOGGER_INFO_GLOBAL", resp.data)
    log_verify("条目逐器真值 + 主闸态独立单查：两层状态各自真实呈现，输出归因唯一"
               "（条目 off 是逐器层所致、条目 on 而无输出查主闸）")


# 场景 6：收班清档（DELETE 两表清理、磁盘保留、惰性重建）
def show_arm_log_cleanup(log_dir):
    log_scene(
        6,
        "收班清档",
        "file_log_delete + 惰性重建",
        "收班下线文件器与默认日志器：DELETE 按名同时清理 console+file 两表并返回"
        "各清了几条，磁盘 .log 文件保留（数据保全归调用方）；删除后再写自动重建。")

    log_action("清档安全黑匣子 blackbox", "FILE_LOG_DELETE blackbox",
               lambda: YomkApi.file_log_delete("blackbox"))
    log_action("清档班次存档 shiftlog", "FILE_LOG_DELETE shiftlog",
               lambda: YomkApi.file_log_delete("shiftlog"))
    log_verify("msg=deleted console:0 file:1：blackbox 只在 file 表，DELETE 报告两表各自清理数")

    log_action("清档未注册日志器 ghost", "FILE_LOG_DELETE ghost",
               lambda: YomkApi.file_log_delete("ghost"))
    log_verify("两表均未命中 -> eNo(logger not found.)：not-found 是框架统一惯例")

    log_action("清档默认日志器 MainLogger", "FILE_LOG_DELETE MainLogger",
               lambda: YomkApi.file_log_delete("MainLogger"))
    log_verify("msg=deleted console:1 file:0：MainLogger 只在 console 表（框架预创建）")

    log_action_start("回读班次存档文件确认保留")
    YomkApi.console_log_info_tag(
        "arm.demo", "---- shiftlog.log 内容（日志器已删、磁盘仍在） ----")
    log_file_content(Path(log_dir) / "shiftlog.log")
    log_verify("DELETE 只清内存表：磁盘 .log 文件保留，数据保全归调用方")

    log_action("再写默认日志（验证惰性重建）", "CONSOLE_LOG_INFO",
               lambda: YomkApi.console_log_info("MainLogger recreated on demand"))
    log_action_start("复查控制台清单确认重建")
    resp = YomkApi.logger_info_loggers("console")
    log_resp("LOGGER_INFO_LOGGERS console", resp)
    if resp.data:
        for logger_name in resp.data:
            log_logger_line("LOGGER_INFO_LOGGERS", logger_name)
    log_verify("MainLogger 回归清单：日志器按名惰性创建，无需手动注册；班次结束，arm.demo 叙事仍在")


def main():
    # 初始化框架（内置日志服务 /YomkLogger 随之自动启动，MainLogger 预创建）
    YomkApi.init()

    YomkApi.console_log_info_tag(
        "arm.demo", "机械臂产线 x YomkLogger 示例：一次班次贯穿 6 个场景，覆盖全部日志 API")
    YomkApi.console_log_info_tag(
        "arm.demo",
        "日志按 tag 分层：arm.demo=叙事与结果 / MainLogger=默认日志器 / "
        "arm.joint=关节模块（演示自定义 tag）/ blackbox、shiftlog=文件器")

    # 日志目录：当前工作目录下的 YomkLog/（场景 3/4 创建文件日志器使用，目录由
    # 框架自动创建）。不解析启动脚本路径——直接用 cwd 即可在启动控制台所在目录落盘
    log_dir = str(Path.cwd() / "YomkLog")

    show_arm_log_levels()
    show_arm_silent_mode()
    show_arm_blackbox(log_dir)
    show_arm_shift_log(log_dir)
    show_arm_logger_dashboard()
    show_arm_log_cleanup(log_dir)

    YomkApi.console_log_info_tag(
        "arm.demo", "========== 示例结束：按回车退出 ==========")
    input()


if __name__ == "__main__":
    main()
