#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerLifecycle.py —— YomkLogger 服务全链路生命周期测试
（LG1-LG6 轮交付）

覆盖内容（9 节）：
S1 控制台日志基线（四级别 + 开关翻转）；S2 自定义 tag 自动创建（含 65536 超长 tag、
空内容）；S2.5 非法级别 default 降级（须在 S3 设 proxy 前）；S3 proxy 契约（拦截/
穿透/卸载恢复/重装复位，LG6 P4-e）；S4 文件日志闭环（create/缓冲/落盘/重复/未创建/
P2-a 空名空 dir/P2-b 失败不注册幽灵）；S5 内省三端点（行格式/字典序/开关联动精确串）；
S6 边界值（整数边界/浮点特殊值/1MB 内容/非法级别 file_log+off/on 无副作用）；S7 异常
路径（空包/错类型包/空 logger 名回退/未知端点）；S8 /delete_logger 全语义（8.1-8.7）

语言机制说明：
- stdout 捕获用 contextlib.redirect_stdout；CHECK 在捕获作用域外（LG1 教训，
  控制台写入经 capture 包装后取响应断言）
- 内省数据解包后 YomkResponse.data 直接是 list[str]
- 浮点格式化 str(-0.0) 为 "-0.0"：子串断言 "d= -0" 仍命中
  （前缀包含），书面记录微差
- S8.4 析构落盘确定性：服务层 delete_logger
  锁外显式释放最后引用，返回前 __del__ 已落盘（C2 语义保证）
- file 日志便捷口径：file_log_info_tag(file,
  "MainLogger", *args) 为固定缺省 tag 形态；file_log_info_tag(file, tag, *args)
  为自定义 tag 形态

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

import YomkApi
from YomkServerPy import Log, ResStatus

g_failed = 0
TIME_RE = re.compile(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]")


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def capture(func):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        return func(), buf.getvalue()


def check_time_format(line):
    return TIME_RE.match(line) is not None


def count_lines(content):
    return content.count("\n")


def read_file(path):
    with open(path) as f:
        return f.read()


def main():
    server = YomkApi.init(1)
    check(server is not None, "YomkApi.init 返回非空服务器")

    tmp_dir = os.path.join(tempfile.gettempdir(),
                           "yomk_logger_lg1_" + str(os.getpid()))
    os.makedirs(tmp_dir, exist_ok=True)
    check(os.path.isdir(tmp_dir), f"临时目录创建成功: {tmp_dir}")

    # ============ Section 1: 控制台日志基线（四级别 + 开关） ============
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "MainLogger", "lg1_s1_info_marker ", 42))
    check(r.status == ResStatus.eOk, "YOMK_INFO 返回 eOk")
    check(r.msg == "success.", "YOMK_INFO 契约消息 success.")
    check("lg1_s1_info_marker  42" in out, "INFO 输出含 marker 与整数变参")
    check("[Info ] [MainLogger] [" in out,
          "INFO 输出格式 [Info ] [MainLogger] [行号]")
    fmt_ok = False
    for line in out.splitlines():
        if "lg1_s1_info_marker" in line:
            fmt_ok = check_time_format(line)
    check(fmt_ok, "INFO 输出行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]")

    r, out = capture(lambda: YomkApi.console_log_warn_tag(
        "MainLogger", "lg1_s1_warn_marker"))
    check(r.status == ResStatus.eOk, "YOMK_WARN 返回 eOk")
    check("[Warn ] [MainLogger]" in out and "lg1_s1_warn_marker" in out,
          "WARN 输出格式与 marker")
    r, out = capture(lambda: YomkApi.console_log_error_tag(
        "MainLogger", "lg1_s1_error_marker"))
    check(r.status == ResStatus.eOk, "YOMK_ERROR 返回 eOk")
    check("[Error] [MainLogger]" in out and "lg1_s1_error_marker" in out,
          "ERROR 输出格式与 marker")
    r, out = capture(lambda: YomkApi.console_log_debug_tag(
        "MainLogger", "lg1_s1_debug_marker"))
    check(r.status == ResStatus.eOk, "YOMK_DEBUG 返回 eOk")
    check("[Debug] [MainLogger]" in out and "lg1_s1_debug_marker" in out,
          "DEBUG 输出格式与 marker")

    # 级别开关：OFF -> 无输出仍 eOk -> ON -> 恢复输出（四级别逐一）
    for off_fn, on_fn, log_fn, name in [
        (YomkApi.off_console_log_info, YomkApi.on_console_log_info,
         lambda m: YomkApi.console_log_info_tag("MainLogger", m), "INFO"),
        (YomkApi.off_console_log_warn, YomkApi.on_console_log_warn,
         lambda m: YomkApi.console_log_warn_tag("MainLogger", m), "WARN"),
        (YomkApi.off_console_log_error, YomkApi.on_console_log_error,
         lambda m: YomkApi.console_log_error_tag("MainLogger", m), "ERROR"),
        (YomkApi.off_console_log_debug, YomkApi.on_console_log_debug,
         lambda m: YomkApi.console_log_debug_tag("MainLogger", m), "DEBUG"),
    ]:
        check(off_fn().status == ResStatus.eOk,
              f"OFF_CONSOLE_LOG_{name} 返回 eOk")
        r, out = capture(lambda: log_fn(f"lg1_s1_off_{name.lower()}_marker"))
        check(r.status == ResStatus.eOk, f"{name} 级别关闭后调用仍 eOk")
        check(f"lg1_s1_off_{name.lower()}_marker" not in out,
              f"{name} 级别关闭后无输出")
        check(on_fn().status == ResStatus.eOk, f"ON_CONSOLE_LOG_{name} 返回 eOk")
        r, out = capture(lambda: log_fn(f"lg1_s1_on_{name.lower()}_marker"))
        check(r.status == ResStatus.eOk, f"{name} 级别重开后调用 eOk")
        check(f"lg1_s1_on_{name.lower()}_marker" in out, f"{name} 级别重开后恢复输出")

    # ============ Section 2: 自定义 tag（自动创建 console logger） ============
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "lg1_tag_logger", "lg1_s2_tag_marker"))
    check(r.status == ResStatus.eOk, "YOMK_INFO_TAG 新 tag 返回 eOk")
    check("[lg1_tag_logger]" in out and "lg1_s2_tag_marker" in out,
          "新 tag 自动创建 console logger 并以其名输出")

    long_tag = "T" * 65536
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        long_tag, "lg1_s2_longtag_marker"))
    check(r.status == ResStatus.eOk, "65536 字符超长 tag 返回 eOk")
    check("lg1_s2_longtag_marker" in out, "超长 tag 日志正常输出")
    check("[" + long_tag + "]" in out, "超长 tag 完整回显到输出行")

    r, out = capture(lambda: YomkApi.console_log_info_tag("lg1_tag_empty", ""))
    check(r.status == ResStatus.eOk, "空内容日志返回 eOk")
    check("[lg1_tag_empty]" in out, "空内容日志仍输出 tag 头（内容为空）")

    # ============ Section 2.5: 非法级别 default 降级（须在 S3 设 proxy 前） ============
    r, out = capture(lambda: YomkApi.request(
        "/YomkLogger/console_log", Log(99, "lg1_s2p5_badlevel_marker", "MainLogger")))
    check(r.status == ResStatus.eOk,
          "非法级别 99 console_log 返回 eOk（P2-c 修复后 default 活分支）")
    check("unknown log level, use Info" in out,
          "console_log default 分支错误提示（降级 Info）")
    check("[Info ] [MainLogger] lg1_s2p5_badlevel_marker" in out,
          "非法级别降级 Info 输出 marker")

    # ============ Section 3: proxy 契约（LG6/P4-e 卸载；末尾重装复位） ============
    proxy_hits = []
    proxy_state = {"intercept": True}

    def proxy(log_pkg):
        proxy_hits.append(1)
        return not proxy_state["intercept"]

    check(YomkApi.logger_info_all().data[0] == "console:debug:on info:on warn:on error:on proxy:off",
          "proxy 设置前 ALL 首行 consoleLevelLine 全 on + proxy:off")
    check(YomkApi.set_console_log_proxy(proxy).status == ResStatus.eOk,
          "SET_CONSOLE_LOG_PROXY 返回 eOk")

    # 拦截态：proxy 返回 False -> eOk + 专属消息 + 无控制台输出
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "MainLogger", "lg1_s3_intercept_marker"))
    check(r.status == ResStatus.eOk, "拦截态 console_log 返回 eOk")
    check(r.msg == "console log proxy is success.",
          "拦截态契约消息 console log proxy is success.")
    check("lg1_s3_intercept_marker" not in out, "拦截态无默认控制台输出")
    check(len(proxy_hits) == 1, "proxy 被调用 1 次（原子计数）")

    # 穿透态：proxy 返回 True -> 走默认输出
    proxy_state["intercept"] = False
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "MainLogger", "lg1_s3_pass_marker"))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "穿透态 console_log 返回 eOk + success.")
    check("lg1_s3_pass_marker" in out, "穿透态恢复默认控制台输出")
    check(len(proxy_hits) == 2, "proxy 累计被调用 2 次")

    check("proxy:on" in YomkApi.logger_info_all(
    ).data[0], "proxy 设置后 ALL 首行 proxy:on")

    # LG6（P4-e）：卸载前先把代理切回拦截态，确认代理确实在链路上
    proxy_state["intercept"] = True
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "MainLogger", "lg6_s3_pre_unload_marker"))
    check(r.status == ResStatus.eOk and r.msg == "console log proxy is success.",
          "卸载前拦截态契约消息（代理确实在链路上）")
    check("lg6_s3_pre_unload_marker" not in out, "卸载前拦截态无默认控制台输出")

    # 卸载：传 None（等价空 function）
    check(YomkApi.set_console_log_proxy(None).status == ResStatus.eOk,
          "set_console_log_proxy(None) 卸载返回 eOk")
    check("proxy:off" in YomkApi.logger_info_all().data[0],
          "卸载后 ALL 首行回到 proxy:off（P4-e 前恒报 proxy:on）")

    # 卸载后：恢复框架默认输出，且代理不再被调用
    hits_before = len(proxy_hits)
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "MainLogger", "lg6_s3_post_unload_marker"))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "卸载后 console_log 返回 eOk + success.（走默认路径而非代理短路）")
    check("lg6_s3_post_unload_marker" in out, "卸载后恢复框架默认控制台输出")
    check("[Info ] [MainLogger] [" in out,
          "卸载后输出为框架默认格式 <时间戳> [Info ] [logger] msg")
    check(len(proxy_hits) == hits_before, "卸载后代理不再被调用（命中数不增）")

    # 复位：重装同一代理并回到穿透态，维持 S4-S7 依赖的 proxy:on + 穿透不变量
    proxy_state["intercept"] = False
    check(YomkApi.set_console_log_proxy(proxy).status == ResStatus.eOk,
          "重装代理返回 eOk（卸载可逆）")
    check("proxy:on" in YomkApi.logger_info_all().data[0],
          "重装后 ALL 首行恢复 proxy:on（S4-S7 不变量已复位）")

    # ============ Section 4: 文件日志闭环 ============
    log_dir = os.path.join(tmp_dir, "s4")
    os.makedirs(log_dir, exist_ok=True)
    log_file = os.path.join(log_dir, "lg1_file_logger.log")

    check(YomkApi.file_log_create(log_dir, "lg1_file_logger").status == ResStatus.eOk,
          "FILE_LOG_CREATE 返回 eOk")
    check(os.path.isfile(log_file) and os.path.getsize(log_file) == 0,
          "CREATE 后 <dir>/<name>.log 空文件已建")

    # 四级别入缓冲：write 前文件仍空（缓冲语义）
    check(YomkApi.file_log_info_tag("lg1_file_logger", "MainLogger", "lg1_s4_info_marker ", 7).status
          == ResStatus.eOk, "FILE_INFO 返回 eOk")
    check(YomkApi.file_log_warn_tag("lg1_file_logger", "MainLogger", "lg1_s4_warn_marker").status
          == ResStatus.eOk, "FILE_WARN 返回 eOk")
    check(YomkApi.file_log_error_tag("lg1_file_logger", "MainLogger", "lg1_s4_error_marker").status
          == ResStatus.eOk, "FILE_ERROR 返回 eOk")
    check(YomkApi.file_log_debug_tag("lg1_file_logger", "MainLogger", "lg1_s4_debug_marker").status
          == ResStatus.eOk, "FILE_DEBUG 返回 eOk")
    check(os.path.getsize(log_file) == 0, "WRITE 前文件仍为空（缓冲语义）")

    # 落盘：读回断言行数/行格式/marker/tag
    check(YomkApi.file_log_write("lg1_file_logger").status == ResStatus.eOk,
          "FILE_LOG_WRITE 返回 eOk")
    content = read_file(log_file)
    check(count_lines(content) == 4, "落盘内容恰 4 行（四级别各一）")
    check("[Info ] [MainLogger]" in content and "[Info ] [MainLogger] [" in content and
          "lg1_s4_info_marker  7" in content,
          "Info 行格式（时间戳+[Info ]+[MainLogger]+[行号] 内容）——FILE_INFO 默认内容 tag 为 "
          "MainLogger，行号独立成段，logger 名仅作路由 key")
    check(YomkApi.file_log_info_tag("lg1_file_logger", "lg1CustomTag", "lg1_s4_customtag_marker").status
          == ResStatus.eOk, "FILE_INFO_TAG 自定义 tag 返回 eOk")
    check(YomkApi.file_log_write("lg1_file_logger").status == ResStatus.eOk,
          "自定义 tag 内容 WRITE eOk")
    content_tag = read_file(log_file)
    check("[Info ] [lg1CustomTag] [" in content_tag and "lg1_s4_customtag_marker" in content_tag,
          "自定义 tag 行格式（[Info ] [tag] [行号] 内容）")
    check("[Warn ]" in content and "[Error]" in content and "[Debug]" in content,
          "Warn/Error/Debug 三级别行均在")
    check(all(check_time_format(l) for l in content.splitlines() if "[Info ]" in l),
          "文件日志行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]")

    # 落盘后缓冲清空：再次 WRITE 文件不增长
    size_before = os.path.getsize(log_file)
    check(YomkApi.file_log_write("lg1_file_logger").status == ResStatus.eOk,
          "二次 FILE_LOG_WRITE 返回 eOk")
    check(os.path.getsize(log_file) == size_before, "二次 WRITE 无重复内容（缓冲已清空）")

    # 重复创建 eNo
    dup = YomkApi.file_log_create(log_dir, "lg1_file_logger")
    check(dup.status == ResStatus.eNo, "重复创建同名 file logger 返回 eNo")
    check(dup.msg == "logger name already exists.", "重复创建契约消息")

    # 未创建 logger：FILE_LOG / FILE_LOG_WRITE 均 eNo
    no_logger = YomkApi.file_log_info_tag(
        "lg1_never_created", "MainLogger", "x")
    check(no_logger.status == ResStatus.eNo, "FILE_LOG 未创建 logger 返回 eNo")
    check(no_logger.msg == "file logger not found.", "FILE_LOG not-found 契约消息")
    no_write = YomkApi.file_log_write("lg1_never_created")
    check(no_write.status == ResStatus.eNo, "FILE_LOG_WRITE 未创建 logger 返回 eNo")
    check(no_write.msg == "logger not found.", "FILE_LOG_WRITE not-found 契约消息")

    # P2-a 处置验证：空名/空 dir 一律 eInvalid
    empty_both = YomkApi.file_log_create("", "")
    check(empty_both.status == ResStatus.eInvalid and empty_both.msg == "logger name is empty.",
          "空名+空 dir 创建返回 eInvalid + logger name is empty.（P2-a 已处置）")
    empty_name = YomkApi.file_log_create(tmp_dir, "")
    check(empty_name.status == ResStatus.eInvalid and empty_name.msg == "logger name is empty.",
          "空名+有效 dir 创建返回 eInvalid（P2-a 已处置）")
    empty_dir = YomkApi.file_log_create("", "lg1_empty_dir_name")
    check(empty_dir.status == ResStatus.eInvalid and empty_dir.msg == "logger dir is empty.",
          "有效名+空 dir 创建返回 eInvalid + logger dir is empty.（P2-a 已处置）")

    # P2-b 处置验证：不可创建 dir -> init False -> eNo 且不注册幽灵 logger
    bad_dir = YomkApi.file_log_create(
        "/proc/lg1_impossible/sub", "lg1_bad_dir_logger")
    check(bad_dir.status == ResStatus.eNo and bad_dir.msg == "init file logger failed.",
          "不可创建 dir 返回 eNo + init file logger failed.（P2-b 已处置，异常不穿透）")
    ghost_info = YomkApi.logger_info_logger("lg1_bad_dir_logger")
    check(ghost_info.status == ResStatus.eNo and ghost_info.msg == "logger not found.",
          "init 失败的 logger 未注册（无幽灵 key，内省 not found）")
    # init 失败不占用名字：同名换有效 dir 重试应成功
    retry_dir = os.path.join(tmp_dir, "s4_retry")
    retry = YomkApi.file_log_create(retry_dir, "lg1_bad_dir_logger")
    check(retry.status == ResStatus.eOk and
          os.path.isfile(os.path.join(retry_dir, "lg1_bad_dir_logger.log")),
          "init 失败后同名换有效 dir 重试成功（名字未被幽灵占用）")

    # ============ Section 5: 内省三端点 ============
    loggers_resp = YomkApi.logger_info_loggers()
    check(loggers_resp.status == ResStatus.eOk, "LOGGER_INFO_LOGGERS 返回 eOk")
    check("MainLogger [console]" in loggers_resp.data,
          "LOGGERS 含 MainLogger [console] 行")
    check(any(l.startswith("lg1_tag_logger") and l.endswith(" [console]")
              for l in loggers_resp.data),
          "LOGGERS 含 S2 自建 tag logger 行（tag [console]）")
    check("lg1_file_logger [file] dir:" + log_dir in loggers_resp.data,
          "LOGGERS 含 file logger 行（名 [file] dir:路径）")

    console_info = YomkApi.logger_info_logger("MainLogger")
    check(console_info.status == ResStatus.eOk and console_info.msg == "MainLogger [console]",
          "LOGGER(MainLogger) 命中 console 格式")
    file_info = YomkApi.logger_info_logger("lg1_file_logger")
    check(file_info.status == ResStatus.eOk and
          file_info.msg == "lg1_file_logger [file] dir:" + log_dir,
          "LOGGER(lg1_file_logger) 命中 file 格式（含 dir）")
    no_info = YomkApi.logger_info_logger("lg1_never_registered")
    check(no_info.status == ResStatus.eNo and no_info.msg == "logger not found.",
          "LOGGER 未注册名返回 eNo + logger not found.")

    # ALL：首行 consoleLevelLine 与开关翻转联动（精确串断言）
    check(YomkApi.logger_info_all().data[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "ALL 首行全 on + proxy:on（S3 已设 proxy）")
    YomkApi.off_console_log_info()
    check(YomkApi.logger_info_all().data[0] == "console:debug:on info:off warn:on error:on proxy:on",
          "OFF INFO 后 ALL 首行 info:off（开关联动精确串）")
    YomkApi.on_console_log_info()
    check(YomkApi.logger_info_all().data[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "ON INFO 复原后 ALL 首行 info:on")

    # ============ Section 6: 边界值（数值特殊值 / 超大内容 / 非法级别） ============
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "lg1_s6_num", "i=", 2147483647, " j=", -2147483648, " z=", 0, " n=", -1))
    check(r.status == ResStatus.eOk, "INT_MAX/INT_MIN/0/-1 变参日志返回 eOk")
    check("i= 2147483647" in out and "j= -2147483648" in out and
          "z= 0" in out and "n= -1" in out,
          "整数边界值字符串化正确（变参以空格分隔）")

    nan_v = float("nan")
    inf_v = float("inf")
    r, out = capture(lambda: YomkApi.console_log_info_tag(
        "lg1_s6_flt", "a=", nan_v, " b=", inf_v, " c=", -inf_v, " d=", -0.0))
    check(r.status == ResStatus.eOk, "NaN/Inf/-Inf/-0.0 变参日志返回 eOk")
    # 微差书面记录：python str(-0.0) 为 "-0.0"，子串断言 "d= -0" 以
    # 前缀包含命中
    check("a= nan" in out and "b= inf" in out and "c= -inf" in out and "d= -0" in out,
          "浮点特殊值字符串化正确（nan/inf/-inf/-0，变参以空格分隔）")

    # 1MB 超大内容：console + file 双路径
    big = "B" * (1024 * 1024)
    r, out = capture(lambda: YomkApi.console_log_info_tag("lg1_s6_big", big))
    check(r.status == ResStatus.eOk, "1MB 超大内容控制台日志返回 eOk")
    check(big in out, "1MB 内容完整输出到控制台")

    big_dir = os.path.join(tmp_dir, "s6")
    os.makedirs(big_dir, exist_ok=True)
    check(YomkApi.file_log_create(big_dir, "lg1_big_logger").status == ResStatus.eOk,
          "超大内容用 file logger 创建 eOk")
    check(YomkApi.file_log_info_tag("lg1_big_logger", "MainLogger", big).status == ResStatus.eOk,
          "1MB 超大内容文件日志返回 eOk")
    check(YomkApi.file_log_write("lg1_big_logger").status == ResStatus.eOk,
          "超大内容 WRITE 返回 eOk")
    content = read_file(os.path.join(big_dir, "lg1_big_logger.log"))
    check(big in content, "1MB 内容完整落盘")

    # 非法级别注入回填（P2-c 修复后）：file_log/off/on 三处 default 降级与无副作用实证
    bad_level_dir = os.path.join(tmp_dir, "s6_badlevel")
    os.makedirs(bad_level_dir, exist_ok=True)
    check(YomkApi.file_log_create(bad_level_dir, "lg1_s6_badlevel").status == ResStatus.eOk,
          "非法级别注入用 file logger 创建 eOk")
    r, out = capture(lambda: YomkApi.request(
        "/YomkLogger/file_log", Log(99, "lg1_s6_badlevel_marker", "lg1_s6_badlevel")))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "file_log 非法级别 99 返回 eOk（P2-c 修复后 default 活分支）")
    check("unknown log level, use Info" in out,
          "file_log default 分支错误提示（降级 Info）")
    check(YomkApi.file_log_write("lg1_s6_badlevel").status == ResStatus.eOk,
          "非法级别落盘 WRITE eOk")
    content = read_file(os.path.join(bad_level_dir, "lg1_s6_badlevel.log"))
    check(count_lines(content) == 1 and "[Info ] lg1_s6_badlevel_marker" in content,
          "非法级别降级 Info 落盘恰 1 行")

    # off/on 非法级别：default 仅记录错误，无副作用仍 eOk，开关状态不变
    r_off, out_off = capture(lambda: YomkApi.request(
        "/YomkLogger/off_console_log_by_level", Log(99, "", "")))
    check(r_off.status == ResStatus.eOk and "unknown log level, turn off failed." in out_off,
          "off 非法级别 eOk + 错误提示（default 活分支）")
    r_on, out_on = capture(lambda: YomkApi.request(
        "/YomkLogger/on_console_log_by_level", Log(99, "", "")))
    check(r_on.status == ResStatus.eOk and "unknown log level, turn on failed." in out_on,
          "on 非法级别 eOk + 错误提示（default 活分支）")
    check(YomkApi.logger_info_all().data[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "非法级别 off/on 无副作用（ALL 首行仍全 on）")

    # ============ Section 7: 异常路径（空包 / 错类型包 / 未知端点） ============
    r, out = capture(lambda: YomkApi.request("/YomkLogger/console_log", None))
    check(r.status == ResStatus.eNo, "console_log nullptr 包返回 eNo")
    check(r.msg == " pkg is null or pkg is not Log. ", "console_log 空包契约消息")
    r = YomkApi.request("/YomkLogger/console_log", "wrong")
    check(r.status == ResStatus.eNo, "console_log 错类型包返回 eNo")
    check(r.msg == " pkg is null or pkg is not Log. ", "console_log 错类型包契约消息")
    check(YomkApi.request("/YomkLogger/file_log", None).status == ResStatus.eNo,
          "file_log nullptr 包返回 eNo")
    check(YomkApi.request("/YomkLogger/write_file_log", None).status == ResStatus.eNo,
          "write_file_log nullptr 包返回 eNo")
    check(YomkApi.request("/YomkLogger/create_file_logger", None).status == ResStatus.eNo,
          "create_file_logger nullptr 包返回 eNo")
    check(YomkApi.request("/YomkLogger/logger", None).status == ResStatus.eNo,
          "logger nullptr 包返回 eNo")
    # console_log 空 logger 名 -> 回退 MainLogger 分支（raw 注入才可触达）
    r, out = capture(lambda: YomkApi.request(
        "/YomkLogger/console_log", Log(Log.eInfo, "lg1_s7_empty_logger_marker", "")))
    check(r.status == ResStatus.eOk, "console_log 空 logger 名返回 eOk（回退分支）")
    check("console logger name is empty, use MainLogger" in out,
          "空 logger 名错误提示（回退 MainLogger）")
    check("[MainLogger] lg1_s7_empty_logger_marker" in out,
          "空 logger 名回退 MainLogger 输出")
    # file_log 空 logger 名 -> eNo 分支
    r, out = capture(lambda: YomkApi.request(
        "/YomkLogger/file_log", Log(Log.eInfo, "x", "")))
    check(r.status == ResStatus.eNo and r.msg == "file logger name is empty.",
          "file_log 空 logger 名返回 eNo + 契约消息")
    check("file logger name is empty." in out, "file_log 空名错误提示")
    # LOGGER_INFO_LOGGER 空名 -> eNo（P2-a 堵死空名注册后无幽灵 key）
    empty_name_info = YomkApi.logger_info_logger("")
    check(empty_name_info.status == ResStatus.eNo and empty_name_info.msg == "logger not found.",
          "LOGGER 空名返回 eNo + logger not found.（P2-a 处置后无幽灵 key）")
    # 未知端点 -> invoke 路由层 eNo
    unknown = YomkApi.request("/YomkLogger/no_such_func", None)
    check(unknown.status == ResStatus.eNo, "未知端点返回 eNo（invoke not found）")
    check(unknown.msg == "function not found: /no_such_func",
          "未知端点契约消息（路由剥离服务前缀）")

    # ============ Section 8: logger 删除接口（LG4 /delete_logger 全语义） ============
    # 8.1 空名 -> eInvalid（与 createFileLogger 的 P2-a 空名惯例同口径）
    r, out = capture(lambda: YomkApi.file_log_delete(""))
    check(r.status == ResStatus.eInvalid and r.msg == "logger name is empty.",
          "S8: 删除空名返回 eInvalid + 契约消息")
    check("logger name is empty." in out, "S8: 空名删除错误提示")

    # 8.2 两表均未命中 -> eNo + not-found 契约消息
    r, out = capture(lambda: YomkApi.file_log_delete("lg1_s8_never_created"))
    check(r.status == ResStatus.eNo and r.msg == "logger not found.",
          "S8: 删除未注册 logger 返回 eNo + logger not found.")
    check("logger: lg1_s8_never_created not found." in out, "S8: 未命中错误提示含 logger 名")

    # 8.3 console-only：msg 计数 1/0；删除后单查 eNo；同名再写惰性重建
    name = "lg1_s8_console_only"
    r, _ = capture(lambda: YomkApi.console_log_info_tag(
        name, "lg1_s8_console_marker"))
    check(r.status == ResStatus.eOk, "S8: console-only 预热创建 eOk")
    check(YomkApi.logger_info_logger(name).status ==
          ResStatus.eOk, "S8: 创建后单查命中 eOk")
    r, _ = capture(lambda: YomkApi.file_log_delete(name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:1 file:0",
          f"S8: console-only 删除 eOk + msg 计数: {r.msg}")
    check(YomkApi.logger_info_logger(name).status ==
          ResStatus.eNo, "S8: console 删除后单查 eNo")
    # 惰性重建：同名再写一条 -> consoleLog 双检锁重建路径
    r, _ = capture(lambda: YomkApi.console_log_info_tag(
        name, "lg1_s8_rebuild_marker"))
    check(r.status == ResStatus.eOk, "S8: 删除后同名再写 eOk（惰性重建）")
    check(YomkApi.logger_info_logger(name).status == ResStatus.eOk,
          "S8: 惰性重建后单查恢复 eOk")
    check(YomkApi.file_log_delete(name).status ==
          ResStatus.eOk, "S8: 清理重建 logger eOk")

    # 8.4 file-only：写 2 行不 flush -> 删除释放最后引用 -> __del__ 锁外自动落盘
    dir84 = os.path.join(tmp_dir, "s8_file")
    os.makedirs(dir84, exist_ok=True)
    name = "lg1_s8_file_only"
    check(YomkApi.file_log_create(dir84, name).status ==
          ResStatus.eOk, "S8: file logger 创建 eOk")
    check(YomkApi.file_log_info_tag(name, "s8Tag", "lg1_s8_file_marker_1").status == ResStatus.eOk,
          "S8: file 写入第 1 行 eOk")
    check(YomkApi.file_log_info_tag(name, "s8Tag", "lg1_s8_file_marker_2").status == ResStatus.eOk,
          "S8: file 写入第 2 行 eOk")
    # 前置对照：未 flush 前 init 建的空文件仍为空
    content_before = read_file(os.path.join(dir84, name + ".log"))
    check(content_before == "", "S8: 删除前未 flush，文件仍为空")
    r, _ = capture(lambda: YomkApi.file_log_delete(name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:0 file:1",
          f"S8: file-only 删除 eOk + msg 计数: {r.msg}")
    content = read_file(os.path.join(dir84, name + ".log"))
    check(count_lines(content) == 2, "S8: 删除触发析构自动落盘恰 2 行")
    check("lg1_s8_file_marker_1" in content and "lg1_s8_file_marker_2" in content,
          "S8: 落盘内容含两条 marker（数据不因删除丢失）")
    check(YomkApi.file_log_write(name).status ==
          ResStatus.eNo, "S8: 删除后 WRITE 同名返回 eNo")

    # 8.5 同名双表：一次删除同时清理两表（msg 计数 1/1），重复删除幂等 eNo
    dir85 = os.path.join(tmp_dir, "s8_both")
    os.makedirs(dir85, exist_ok=True)
    name = "lg1_s8_both_tables"
    r, _ = capture(lambda: YomkApi.console_log_info_tag(
        name, "lg1_s8_both_console_marker"))
    check(r.status == ResStatus.eOk, "S8: 双表用例 console 侧创建 eOk")
    check(YomkApi.file_log_create(dir85, name).status ==
          ResStatus.eOk, "S8: 双表用例 file 侧创建 eOk")
    r, _ = capture(lambda: YomkApi.file_log_delete(name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:1 file:1",
          f"S8: 同名双表一次删除 eOk + msg 计数: {r.msg}")
    check(YomkApi.logger_info_logger(name).status == ResStatus.eNo,
          "S8: 双表删除后单查 eNo（两表均已清理）")
    r, out2 = capture(lambda: YomkApi.file_log_delete(name))
    check(r.status == ResStatus.eNo and r.msg == "logger not found.",
          "S8: 重复删除返回 eNo（幂等）")
    check("logger: lg1_s8_both_tables not found." in out2,
          "S8: 重复删除错误提示含 logger 名")

    # 8.6 内省行数递减守恒 + ALL 与 LOGGERS 行数关系 + MainLogger 删除后惰性重建
    dir86 = os.path.join(tmp_dir, "s8_count")
    os.makedirs(dir86, exist_ok=True)
    name = "lg1_s8_count"
    before = len(YomkApi.logger_info_loggers().data)
    r, _ = capture(lambda: YomkApi.console_log_info_tag(
        name, "lg1_s8_count_marker"))
    check(r.status == ResStatus.eOk, "S8: 计数用例 console 侧创建 eOk")
    check(YomkApi.file_log_create(dir86, name).status == ResStatus.eOk,
          "S8: 计数用例 file 侧创建 eOk")
    check(len(YomkApi.logger_info_loggers().data) == before + 2,
          "S8: 双表各建一个 -> LOGGERS 行数 +2")
    check(YomkApi.file_log_delete(name).status ==
          ResStatus.eOk, "S8: 计数用例删除 eOk")
    check(len(YomkApi.logger_info_loggers().data) == before,
          "S8: 删除后 LOGGERS 行数回落（递减守恒）")
    all_lines = YomkApi.logger_info_all().data
    check(len(all_lines) == before + 1 and
          all_lines[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "S8: ALL 行数 = LOGGERS 行数 + 状态行，首行精确串不变")

    # MainLogger 无特殊保护：可删；重建须走"空 logger 名回退"路径
    check(YomkApi.file_log_delete("MainLogger").status == ResStatus.eOk,
          "S8: MainLogger 可删除 eOk（无特殊保护）")
    check(YomkApi.logger_info_logger("MainLogger").status == ResStatus.eNo,
          "S8: MainLogger 删除后单查 eNo")
    r, rebuild_out = capture(lambda: YomkApi.request(
        "/YomkLogger/console_log", Log(Log.eInfo, "lg1_s8_mainlogger_rebuild_marker", "")))
    check(r.status == ResStatus.eOk, "S8: MainLogger 删除后空名回退写入 eOk")
    check(YomkApi.logger_info_logger("MainLogger").status == ResStatus.eOk,
          "S8: MainLogger 惰性重建后单查恢复 eOk")
    check("[MainLogger] lg1_s8_mainlogger_rebuild_marker" in rebuild_out,
          "S8: 重建对象名确为 MainLogger（输出行 [MainLogger]）")
    check(len(YomkApi.logger_info_loggers().data) == before,
          "S8: MainLogger 删除+重建后行数回到基线（无泄漏无重复）")

    # 8.7 异常路径：空包 / 错类型包（YomkUnPackPkgResponse 契约分支）
    r_null = YomkApi.request("/YomkLogger/delete_logger", None)
    check(r_null.status == ResStatus.eNo and r_null.msg == " pkg is null or pkg is not String. ",
          "S8: delete_logger nullptr 包返回 eNo + 契约消息")
    r_wrong = YomkApi.request(
        "/YomkLogger/delete_logger", Log(Log.eInfo, "x", "y"))
    check(r_wrong.status == ResStatus.eNo and r_wrong.msg == " pkg is null or pkg is not String. ",
          "S8: delete_logger 错类型包（Log）返回 eNo + 契约消息")

    # ============ 收尾清理 ============
    YomkApi.shutdown()
    shutil.rmtree(tmp_dir, ignore_errors=True)

    if g_failed == 0:
        print("TestYomkLoggerLifecycle all check passed.")
        return 0
    print(f"TestYomkLoggerLifecycle FAILED ({g_failed} checks failed).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
