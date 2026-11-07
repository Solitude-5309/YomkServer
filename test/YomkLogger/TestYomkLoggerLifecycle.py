#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerLifecycle.py —— YomkLogger 服务全链路生命周期测试

覆盖内容（12 节）：
S1 控制台日志基础态（四级别 + 开关翻转）；S2 自定义 tag 自动创建（含 65536 超长 tag、
空内容）；S2.5 非法级别 default 降级（须在 S3 设 proxy 前）；S3 proxy 契约（拦截/
穿透/卸载恢复/重装复位）；S4 文件日志闭环（create/缓冲/落盘/重复/未创建/
空名空 dir 校验/失败不注册幽灵）；S5 内省三端点（行格式/字典序/开关联动逐字段）；
S6 边界值（整数边界/浮点特殊值/1MB 内容/非法级别 file_log+off/on 无副作用）；S7 异常
路径（空包/错类型包/空 logger 名回退/未知端点）；S8 /delete_logger 全语义（8.1-8.7）；
S9 服务级具名开关与全局态端点（两级主闸并联：具名 OFF/ON 逐器隔离与恢复、未知名
eNo 不惰性创建、主闸×逐器两层归因、条目逐器真值与两层正交、全局主闸初态与
无参翻转联动）；S10 文件日志两级开关与全局态端点（与控制台同构、落盘实证：
具名 OFF/ON 文件逐器隔离与恢复、未知名 eNo、文件主闸×逐器两层归因、
两层正交、console·file global_state 两套主闸独立直证与无参翻转联动、
未知 type eNo）；S11 文件日志全局代理契约（与控制台 S3 同构仅单一全局代理：
两套代理独立直证/拦截/穿透/卸载（镜像控制台卸载流程）/重装可逆，末尾重装回穿透态）

实现机制说明：
- stdout 捕获用 contextlib.redirect_stdout；CHECK 在捕获作用域外
  （控制台写入经 capture 包装后取响应断言）
- 内省数据解包后 LOGGERS 端点 data 为 list[str]，ALL 端点 data 为 list[LoggerInfo] 清单（每条内嵌 logger_state 状态段）
- 浮点格式化 str(-0.0) 为 "-0.0"：子串断言 "d= -0" 仍命中
  （前缀包含），如实注记微差
- S8.4 析构落盘确定性：服务层 delete_logger
  锁外显式释放最后引用，返回前 __del__ 已落盘（服务层显式释放最后引用保证）
- file 日志便捷口径：log().to_sink(file).tag("main").info(*args) 为固定缺省 tag
  形态；tag 换任意值即自定义形态（SinkLogBuilder.tag + 六级发射）

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
from YomkServer import Log, LogFile, ResStatus, LoggerInfo
from YomkApi import log, log_mgr, server

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

def raw_logger(url, pkg):
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息断言经 server 域
    # url/msg/request 裸链直取 Response（端点行为与链式终结一致）
    return YomkApi.server().url(url).msg(pkg).request()

def check_time_format(line):
    return TIME_RE.match(line) is not None

def count_lines(content):
    return content.count("\n")

def read_file(path):
    with open(path) as f:
        return f.read()

def first_console_state(data):
    # 从 ALL 清单取首条 console 条目的控制台状态段（无 console 条目返回 None）
    for lg in (data if data else []):
        if lg.type == "console":
            return lg.logger_state
    return None

def test_tmp_root():
    # 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退系统临时目录
    return os.environ.get("YOMK_TEST_TMP_ROOT") or tempfile.gettempdir()

def main():
    YomkApi.server().async_thread(1).init()
    server = YomkApi.server().instance()
    check(server is not None, "server().init 返回非空服务器")

    tmp_dir = os.path.join(test_tmp_root(),
                           "yomk_logger_lg1_" + str(os.getpid()))
    os.makedirs(tmp_dir, exist_ok=True)
    check(os.path.isdir(tmp_dir), f"临时目录创建成功: {tmp_dir}")

    # ============ Section 1: 控制台日志基础态（四级别 + 开关） ============
    r, out = capture(lambda: raw_logger("/YomkLogger/console_log", Log(Log.eInfo, "[1] lg1_s1_info_marker  42", "main", "main")))
    check(r.status == ResStatus.eOk, "YOMK_INFO 返回 eOk")
    check(r.msg == "success.", "YOMK_INFO 契约消息 success.")
    check("lg1_s1_info_marker  42" in out, "INFO 输出含 marker 与整数变参")
    check("[INFO ] [main] [" in out,
          "INFO 输出格式 [INFO ] [main] [行号]")
    fmt_ok = False
    for line in out.splitlines():
        if "lg1_s1_info_marker" in line:
            fmt_ok = check_time_format(line)
    check(fmt_ok, "INFO 输出行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]")

    r, out = capture(lambda: log().tag("main").warn("lg1_s1_warn_marker"))
    check(r == ResStatus.eOk, "YOMK_WARN 返回 eOk")
    check("[WARN ] [main]" in out and "lg1_s1_warn_marker" in out,
          "WARN 输出格式与 marker")
    r, out = capture(lambda: log().tag("main").error("lg1_s1_error_marker"))
    check(r == ResStatus.eOk, "YOMK_ERROR 返回 eOk")
    check("[ERROR] [main]" in out and "lg1_s1_error_marker" in out,
          "ERROR 输出格式与 marker")
    r, out = capture(lambda: log().tag("main").debug("lg1_s1_debug_marker"))
    check(r == ResStatus.eOk, "YOMK_DEBUG 返回 eOk")
    check("[DEBUG] [main]" in out and "lg1_s1_debug_marker" in out,
          "DEBUG 输出格式与 marker")

    # 级别开关：OFF -> 无输出仍 eOk -> ON -> 恢复输出（四级别逐一）
    for off_fn, on_fn, log_fn, name in [
        (lambda: log_mgr().console().level(Log.eInfo).disable(),
         lambda: log_mgr().console().level(Log.eInfo).enable(),
         lambda m: log().tag("main").info(m), "INFO"),
        (lambda: log_mgr().console().level(Log.eWarn).disable(),
         lambda: log_mgr().console().level(Log.eWarn).enable(),
         lambda m: log().tag("main").warn(m), "WARN"),
        (lambda: log_mgr().console().level(Log.eError).disable(),
         lambda: log_mgr().console().level(Log.eError).enable(),
         lambda m: log().tag("main").error(m), "ERROR"),
        (lambda: log_mgr().console().level(Log.eDebug).disable(),
         lambda: log_mgr().console().level(Log.eDebug).enable(),
         lambda m: log().tag("main").debug(m), "DEBUG"),
    ]:
        check(off_fn() == ResStatus.eOk,
              f"OFF_CONSOLE_LOG_{name} 返回 eOk")
        r, out = capture(lambda: log_fn(f"lg1_s1_off_{name.lower()}_marker"))
        check(r == ResStatus.eOk, f"{name} 级别关闭后调用仍 eOk")
        check(f"lg1_s1_off_{name.lower()}_marker" not in out,
              f"{name} 级别关闭后无输出")
        check(on_fn() == ResStatus.eOk, f"ON_CONSOLE_LOG_{name} 返回 eOk")
        r, out = capture(lambda: log_fn(f"lg1_s1_on_{name.lower()}_marker"))
        check(r == ResStatus.eOk, f"{name} 级别重开后调用 eOk")
        check(f"lg1_s1_on_{name.lower()}_marker" in out, f"{name} 级别重开后恢复输出")

    # ============ Section 2: 自定义 tag（自动创建 console logger） ============
    r, out = capture(lambda: log().tag("lg1_tag_logger").info("lg1_s2_tag_marker"))
    check(r == ResStatus.eOk, "YOMK_INFO_TAG 新 tag 返回 eOk")
    check("[lg1_tag_logger]" in out and "lg1_s2_tag_marker" in out,
          "新 tag 自动创建 console logger 并以其名输出")

    long_tag = "T" * 65536
    r, out = capture(lambda: log().tag(long_tag).info("lg1_s2_longtag_marker"))
    check(r == ResStatus.eOk, "65536 字符超长 tag 返回 eOk")
    check("lg1_s2_longtag_marker" in out, "超长 tag 日志正常输出")
    check("[" + long_tag + "]" in out, "超长 tag 完整回显到输出行")

    r, out = capture(lambda: log().tag("lg1_tag_empty").info(""))
    check(r == ResStatus.eOk, "空内容日志返回 eOk")
    check("[lg1_tag_empty]" in out, "空内容日志仍输出 tag 头（内容为空）")

    # ============ Section 2.5: 非法级别 default 降级（须在 S3 设 proxy 前） ============
    r, out = capture(lambda: raw_logger(
        "/YomkLogger/console_log", Log(99, "lg1_s2p5_badlevel_marker", "main")))
    check(r.status == ResStatus.eOk,
          "非法级别 99 console_log 返回 eOk")
    check("unknown log level, use Info" in out,
          "console_log default 分支错误提示（降级 Info）")
    check("[INFO ] [main] lg1_s2p5_badlevel_marker" in out,
          "非法级别降级 Info 输出 marker")

    # ============ Section 3: proxy 契约（卸载恢复；末尾重装复位） ============
    proxy_hits = []
    proxy_state = {"intercept": True}

    def proxy(log_pkg):
        proxy_hits.append(1)
        return not proxy_state["intercept"]

    all_resp = log_mgr().all()
    check(all_resp.status == ResStatus.eOk and all_resp.msg == "ok"
          and isinstance(all_resp.data, list),
          "ALL 命中返回 eOk + msg ok + list[LoggerInfo] 清单")
    st = first_console_state(all_resp.data)
    check(st and st.debug_on and st.info_on
          and st.warn_on and st.error_on
          and not st.proxy_on,
          "proxy 设置前 ALL console 条目状态段全 on + proxy_off")
    check(log_mgr().console().proxy(proxy).apply() == ResStatus.eOk,
          "SET_CONSOLE_LOG_PROXY 返回 eOk")

    # 拦截态：proxy 返回 False -> eOk + 专属消息 + 无控制台输出
    r, out = capture(lambda: raw_logger("/YomkLogger/console_log", Log(Log.eInfo, "[1] lg1_s3_intercept_marker", "main", "main")))
    check(r.status == ResStatus.eOk, "拦截态 console_log 返回 eOk")
    check(r.msg == "console log proxy is success.",
          "拦截态契约消息 console log proxy is success.")
    check("lg1_s3_intercept_marker" not in out, "拦截态无默认控制台输出")
    check(len(proxy_hits) == 1, "proxy 被调用 1 次（原子计数）")

    # 穿透态：proxy 返回 True -> 走默认输出
    proxy_state["intercept"] = False
    r, out = capture(lambda: raw_logger("/YomkLogger/console_log", Log(Log.eInfo, "[1] lg1_s3_pass_marker", "main", "main")))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "穿透态 console_log 返回 eOk + success.")
    check("lg1_s3_pass_marker" in out, "穿透态恢复默认控制台输出")
    check(len(proxy_hits) == 2, "proxy 累计被调用 2 次")

    check(first_console_state(log_mgr().all().data).proxy_on,
          "proxy 设置后 ALL console 条目状态段 proxy_on")

    # 卸载前先把代理切回拦截态，确认代理确实在链路上
    proxy_state["intercept"] = True
    r, out = capture(lambda: raw_logger("/YomkLogger/console_log", Log(Log.eInfo, "[1] lg6_s3_pre_unload_marker", "main", "main")))
    check(r.status == ResStatus.eOk and r.msg == "console log proxy is success.",
          "卸载前拦截态契约消息（代理确实在链路上）")
    check("lg6_s3_pre_unload_marker" not in out, "卸载前拦截态无默认控制台输出")

    # 卸载：传 None（等价空 function）
    check(log_mgr().console().proxy(None).apply() == ResStatus.eOk,
          "set_console_log_proxy(None) 卸载返回 eOk")
    check(not first_console_state(log_mgr().all().data).proxy_on,
          "卸载后 ALL console 条目状态段回到 proxy_off")

    # 卸载后：恢复框架默认输出，且代理不再被调用
    hits_before = len(proxy_hits)
    r, out = capture(lambda: raw_logger("/YomkLogger/console_log", Log(Log.eInfo, "[1] lg6_s3_post_unload_marker", "main", "main")))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "卸载后 console_log 返回 eOk + success.（走默认路径而非代理短路）")
    check("lg6_s3_post_unload_marker" in out, "卸载后恢复框架默认控制台输出")
    check("[INFO ] [main] [" in out,
          "卸载后输出为框架默认格式 <时间戳> [INFO ] [logger] msg")
    check(len(proxy_hits) == hits_before, "卸载后代理不再被调用（命中数不增）")

    # 复位：重装同一代理并回到穿透态，维持 S4-S7 依赖的 proxy:on + 穿透不变量
    proxy_state["intercept"] = False
    check(log_mgr().console().proxy(proxy).apply() == ResStatus.eOk,
          "重装代理返回 eOk（卸载可逆）")
    check(first_console_state(log_mgr().all().data).proxy_on,
          "重装后 ALL console 条目状态段恢复 proxy_on（S4-S7 不变量已复位）")

    # ============ Section 4: 文件日志闭环 ============
    log_dir = os.path.join(tmp_dir, "s4")
    os.makedirs(log_dir, exist_ok=True)
    log_file = os.path.join(log_dir, "lg1_file_logger.log")

    check(log_mgr().dir(log_dir).file("lg1_file_logger").create() == ResStatus.eOk,
          "FILE_LOG_CREATE 返回 eOk")
    check(os.path.isfile(log_file) and os.path.getsize(log_file) == 0,
          "CREATE 后 <dir>/<name>.log 空文件已建")

    # 四级别入缓冲：write 前文件仍空（缓冲语义）
    check(log().to_sink("lg1_file_logger").tag("main").info("lg1_s4_info_marker ", 7)
          == ResStatus.eOk, "FILE_INFO 返回 eOk")
    check(log().to_sink("lg1_file_logger").tag("main").warn("lg1_s4_warn_marker")
          == ResStatus.eOk, "FILE_WARN 返回 eOk")
    check(log().to_sink("lg1_file_logger").tag("main").error("lg1_s4_error_marker")
          == ResStatus.eOk, "FILE_ERROR 返回 eOk")
    check(log().to_sink("lg1_file_logger").tag("main").debug("lg1_s4_debug_marker")
          == ResStatus.eOk, "FILE_DEBUG 返回 eOk")
    check(os.path.getsize(log_file) == 0, "WRITE 前文件仍为空（缓冲语义）")

    # 落盘：读回断言行数/行格式/marker/tag
    check(log_mgr().file("lg1_file_logger").flush() == ResStatus.eOk,
          "FILE_LOG_WRITE 返回 eOk")
    content = read_file(log_file)
    check(count_lines(content) == 4, "落盘内容恰 4 行（四级别各一）")
    check("[INFO ] [main]" in content and "[INFO ] [main] [" in content and
          "lg1_s4_info_marker  7" in content,
          "Info 行格式（时间戳+[INFO ]+[main]+[行号] 内容）——FILE_INFO 默认内容 tag 为 "
          "main，行号独立成段，logger 名仅作路由 key")
    check(log().to_sink("lg1_file_logger").tag("lg1CustomTag").info("lg1_s4_customtag_marker")
          == ResStatus.eOk, "FILE_INFO_TAG 自定义 tag 返回 eOk")
    check(log_mgr().file("lg1_file_logger").flush() == ResStatus.eOk,
          "自定义 tag 内容 WRITE eOk")
    content_tag = read_file(log_file)
    check("[INFO ] [lg1CustomTag] [" in content_tag and "lg1_s4_customtag_marker" in content_tag,
          "自定义 tag 行格式（[INFO ] [tag] [行号] 内容）")
    check("[WARN ]" in content and "[ERROR]" in content and "[DEBUG]" in content,
          "Warn/Error/Debug 三级别行均在")
    check(all(check_time_format(l) for l in content.splitlines() if "[INFO ]" in l),
          "文件日志行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]")

    # 落盘后缓冲清空：再次 WRITE 文件不增长
    size_before = os.path.getsize(log_file)
    check(log_mgr().file("lg1_file_logger").flush() == ResStatus.eOk,
          "二次 FILE_LOG_WRITE 返回 eOk")
    check(os.path.getsize(log_file) == size_before, "二次 WRITE 无重复内容（缓冲已清空）")

    # 重复创建 eNo
    dup = log_mgr().dir(log_dir).file("lg1_file_logger").create()
    check(dup == ResStatus.eNo, "重复创建同名 file logger 返回 eNo")
    check(raw_logger("/YomkLogger/create_file_logger",
                     LogFile("lg1_file_logger", log_dir)).msg == "logger name already exists.",
          "重复创建契约消息")

    # 未创建 logger：FILE_LOG / FILE_LOG_WRITE 均 eNo
    no_logger = log().to_sink("lg1_never_created").tag("main").info("x")
    check(no_logger == ResStatus.eNo, "FILE_LOG 未创建 logger 返回 eNo")
    check(raw_logger("/YomkLogger/file_log",
                     Log(Log.eInfo, "x", "lg1_never_created", "main")).msg
          == "file logger not found.", "FILE_LOG not-found 契约消息")
    no_write = log_mgr().file("lg1_never_created").flush()
    check(no_write == ResStatus.eNo, "FILE_LOG_WRITE 未创建 logger 返回 eNo")
    check(raw_logger("/YomkLogger/write_file_log", "lg1_never_created").msg
          == "logger not found.", "FILE_LOG_WRITE not-found 契约消息")

    # 空名/空 dir 边界处置验证：一律 eInvalid
    empty_both = log_mgr().dir("").file("").create()
    check(empty_both == ResStatus.eInvalid
          and raw_logger("/YomkLogger/create_file_logger", LogFile("", "")).msg
          == "logger name is empty.",
          "空名+空 dir 创建返回 eInvalid + logger name is empty.")
    empty_name = log_mgr().dir(tmp_dir).file("").create()
    check(empty_name == ResStatus.eInvalid,
          "空名+有效 dir 创建返回 eInvalid")
    empty_dir = log_mgr().dir("").file("lg1_empty_dir_name").create()
    check(empty_dir == ResStatus.eInvalid
          and raw_logger("/YomkLogger/create_file_logger",
                         LogFile("lg1_empty_dir_name", "")).msg == "logger dir is empty.",
          "有效名+空 dir 创建返回 eInvalid + logger dir is empty.")

    # 不可创建 dir 处置验证：init False -> eNo 且不注册幽灵 logger
    bad_dir = log_mgr().dir("/proc/lg1_impossible/sub").file("lg1_bad_dir_logger").create()
    check(bad_dir == ResStatus.eNo
          and raw_logger("/YomkLogger/create_file_logger",
                         LogFile("lg1_bad_dir_logger", "/proc/lg1_impossible/sub")).msg
          == "init file logger failed.",
          "不可创建 dir 返回 eNo + init file logger failed.（异常不穿透）")
    ghost_info = log_mgr().file("lg1_bad_dir_logger").get_info()
    check(ghost_info.status == ResStatus.eNo and ghost_info.msg == "logger not found.",
          "init 失败的 logger 未注册（无幽灵 key，内省 not found）")
    # init 失败不占用名字：同名换有效 dir 重试应成功
    retry_dir = os.path.join(tmp_dir, "s4_retry")
    retry = log_mgr().dir(retry_dir).file("lg1_bad_dir_logger").create()
    check(retry == ResStatus.eOk and
          os.path.isfile(os.path.join(retry_dir, "lg1_bad_dir_logger.log")),
          "init 失败后同名换有效 dir 重试成功（名字未被幽灵占用）")

    # ============ Section 5: 内省三端点 ============
    console_resp = log_mgr().console().list()
    check(console_resp.status == ResStatus.eOk,
          "LOGGER_INFO_LOGGERS(console) 返回 eOk")
    check("main" in console_resp.data,
          "LOGGERS(console) 含 main 纯名行")
    check(any(l.startswith("lg1_tag_logger") for l in console_resp.data),
          "LOGGERS(console) 含 S2 自建 tag logger 纯名行")
    file_resp = log_mgr().file().list()
    check(file_resp.status == ResStatus.eOk
          and "lg1_file_logger" in file_resp.data,
          "LOGGERS(file) 含 lg1_file_logger 纯名行（dir 经 LOGGER_INFO_LOGGER 查询断言覆盖）")

    console_info = log_mgr().console("main").get_info()
    check(console_info.status == ResStatus.eOk and console_info.msg == "ok",
          "LOGGER(main) 命中 msg 恒为 ok")
    check(isinstance(console_info.data, LoggerInfo),
          "LOGGER(main) 数据为 LoggerInfo 包")
    check(console_info.data is not None
          and console_info.data.logger_name == "main"
          and console_info.data.type == "console",
          "LOGGER(main) 字段 logger_name/type 一致")
    check(console_info.data is not None and console_info.data.dir == "",
          "console 命中 dir 为空")
    file_info = log_mgr().file("lg1_file_logger").get_info()
    check(file_info.status == ResStatus.eOk and file_info.msg == "ok",
          "LOGGER(lg1_file_logger) 命中 msg 恒为 ok")
    check(file_info.data is not None
          and file_info.data.logger_name == "lg1_file_logger"
          and file_info.data.type == "file",
          "LOGGER(lg1_file_logger) 字段 logger_name/type 一致")
    check(file_info.data is not None and file_info.data.dir == log_dir,
          "file 命中 dir 为创建目录")
    no_info = log_mgr().console("lg1_never_registered").get_info()
    check(no_info.status == ResStatus.eNo and no_info.msg == "logger not found.",
          "LOGGER 未注册名返回 eNo + logger not found.")

    # ALL：状态段初始值 + 开关翻转联动（逐字段断言）；主闸翻转与条目解耦后，
    # 翻转联动取数迁移至 console·file global_state 端点（全局主闸态）；条目改报逐器层
    # 真值，不受无参翻转影响（正交性在 S9 直证）
    c = first_console_state(log_mgr().all().data)
    check(c.debug_on and c.info_on and c.warn_on and c.error_on and c.proxy_on,
          "ALL console 条目状态段全 on + proxy_on（S3 已设 proxy）")
    log_mgr().console().level(Log.eInfo).disable()
    check(not log_mgr().console().status().data.info_on,
          "OFF INFO 后全局主闸状态 info_off（开关联动逐字段）")
    log_mgr().console().level(Log.eInfo).enable()
    check(log_mgr().console().status().data.info_on,
          "ON INFO 复原后全局主闸状态 info_on")

    # ============ Section 6: 边界值（数值特殊值 / 超大内容 / 非法级别） ============
    r, out = capture(lambda: log().tag("lg1_s6_num").info("i=", 2147483647, " j=", -2147483648, " z=", 0, " n=", -1))
    check(r == ResStatus.eOk, "INT_MAX/INT_MIN/0/-1 变参日志返回 eOk")
    check("i= 2147483647" in out and "j= -2147483648" in out and
          "z= 0" in out and "n= -1" in out,
          "整数边界值字符串化正确（变参以空格分隔）")

    nan_v = float("nan")
    inf_v = float("inf")
    r, out = capture(lambda: log().tag("lg1_s6_flt").info("a=", nan_v, " b=", inf_v, " c=", -inf_v, " d=", -0.0))
    check(r == ResStatus.eOk, "NaN/Inf/-Inf/-0.0 变参日志返回 eOk")
    # 微差注记：python str(-0.0) 为 "-0.0"，子串断言 "d= -0" 以
    # 前缀包含命中
    check("a= nan" in out and "b= inf" in out and "c= -inf" in out and "d= -0" in out,
          "浮点特殊值字符串化正确（nan/inf/-inf/-0，变参以空格分隔）")

    # 1MB 超大内容：console + file 双路径
    big = "B" * (1024 * 1024)
    r, out = capture(lambda: log().tag("lg1_s6_big").info(big))
    check(r == ResStatus.eOk, "1MB 超大内容控制台日志返回 eOk")
    check(big in out, "1MB 内容完整输出到控制台")

    big_dir = os.path.join(tmp_dir, "s6")
    os.makedirs(big_dir, exist_ok=True)
    check(log_mgr().dir(big_dir).file("lg1_big_logger").create() == ResStatus.eOk,
          "超大内容用 file logger 创建 eOk")
    check(log().to_sink("lg1_big_logger").tag("main").info(big) == ResStatus.eOk,
          "1MB 超大内容文件日志返回 eOk")
    check(log_mgr().file("lg1_big_logger").flush() == ResStatus.eOk,
          "超大内容 WRITE 返回 eOk")
    content = read_file(os.path.join(big_dir, "lg1_big_logger.log"))
    check(big in content, "1MB 内容完整落盘")

    # 非法级别注入实证：file_log/off/on 三处 default 降级与无副作用
    bad_level_dir = os.path.join(tmp_dir, "s6_badlevel")
    os.makedirs(bad_level_dir, exist_ok=True)
    check(log_mgr().dir(bad_level_dir).file("lg1_s6_badlevel").create() == ResStatus.eOk,
          "非法级别注入用 file logger 创建 eOk")
    r, out = capture(lambda: raw_logger(
        "/YomkLogger/file_log", Log(99, "lg1_s6_badlevel_marker", "lg1_s6_badlevel")))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "file_log 非法级别 99 返回 eOk")
    check("unknown log level, use Info" in out,
          "file_log default 分支错误提示（降级 Info）")
    check(log_mgr().file("lg1_s6_badlevel").flush() == ResStatus.eOk,
          "非法级别落盘 WRITE eOk")
    content = read_file(os.path.join(bad_level_dir, "lg1_s6_badlevel.log"))
    check(count_lines(content) == 1 and "[INFO ] lg1_s6_badlevel_marker" in content,
          "非法级别降级 Info 落盘恰 1 行")

    # off/on 非法级别：default 仅记录错误，无副作用仍 eOk，开关状态不变
    r_off, out_off = capture(lambda: raw_logger(
        "/YomkLogger/off_console_log_by_level", Log(99, "", "")))
    check(r_off.status == ResStatus.eOk and "unknown log level, turn off failed." in out_off,
          "off 非法级别 eOk + 错误提示")
    r_on, out_on = capture(lambda: raw_logger(
        "/YomkLogger/on_console_log_by_level", Log(99, "", "")))
    check(r_on.status == ResStatus.eOk and "unknown log level, turn on failed." in out_on,
          "on 非法级别 eOk + 错误提示")
    c = first_console_state(log_mgr().all().data)
    check(c.debug_on and c.info_on and c.warn_on and c.error_on and c.proxy_on,
          "非法级别 off/on 无副作用（ALL console 条目状态段仍全 on + proxy_on）")

    # ============ Section 7: 异常路径（空包 / 错类型包 / 未知端点） ============
    r, out = capture(lambda: YomkApi.server().url("/YomkLogger/console_log").request())
    check(r.status == ResStatus.eNo, "console_log 空包返回 eNo")
    check(r.msg == "pkg is null or pkg is not Log", "console_log 空包契约消息")
    r = YomkApi.server().url("/YomkLogger/console_log").msg("wrong").request()
    check(r.status == ResStatus.eNo, "console_log 错类型包返回 eNo")
    check(r.msg == "pkg is null or pkg is not Log", "console_log 错类型包契约消息")
    check(YomkApi.server().url("/YomkLogger/file_log").request().status == ResStatus.eNo,
          "file_log 空包返回 eNo")
    check(YomkApi.server().url("/YomkLogger/write_file_log").request().status == ResStatus.eNo,
          "write_file_log 空包返回 eNo")
    check(YomkApi.server().url("/YomkLogger/create_file_logger").request().status == ResStatus.eNo,
          "create_file_logger 空包返回 eNo")
    check(YomkApi.server().url("/YomkLogger/logger").request().status == ResStatus.eNo,
          "logger 空包返回 eNo")
    # console_log 空 logger 名 -> 回退 main 分支（raw 注入才可触达）
    r, out = capture(lambda: raw_logger(
        "/YomkLogger/console_log", Log(Log.eInfo, "lg1_s7_empty_logger_marker", "")))
    check(r.status == ResStatus.eOk, "console_log 空 logger 名返回 eOk（回退分支）")
    check("console logger name is empty, use main" in out,
          "空 logger 名错误提示（回退 main）")
    check("[main] lg1_s7_empty_logger_marker" in out,
          "空 logger 名回退 main 输出")
    # file_log 空 logger 名 -> eNo 分支
    r, out = capture(lambda: raw_logger(
        "/YomkLogger/file_log", Log(Log.eInfo, "x", "")))
    check(r.status == ResStatus.eNo and r.msg == "file logger name is empty.",
          "file_log 空 logger 名返回 eNo + 契约消息")
    check("file logger name is empty." in out, "file_log 空名错误提示")
    # LOGGER_INFO_LOGGER 空名 -> eNo（空名注册被拒后无幽灵 key）
    empty_name_info = log_mgr().console("").get_info()
    check(empty_name_info.status == ResStatus.eNo and empty_name_info.msg == "logger not found.",
          "LOGGER 空名返回 eNo + logger not found.")
    # 未知端点 -> invoke 路由层 eNo
    unknown = YomkApi.server().url("/YomkLogger/no_such_func").request()
    check(unknown.status == ResStatus.eNo, "未知端点返回 eNo（invoke not found）")
    check(unknown.msg == "function not found: /no_such_func",
          "未知端点契约消息（路由剥离服务前缀）")

    # ============ Section 8: logger 删除接口（/delete_logger 全语义） ============
    # 8.1 空名 -> eInvalid（与 createFileLogger 的空名惯例同口径）
    r, out = capture(lambda: raw_logger("/YomkLogger/delete_logger", ""))
    check(r.status == ResStatus.eInvalid and r.msg == "logger name is empty.",
          "S8: 删除空名返回 eInvalid + 契约消息")
    check("logger name is empty." in out, "S8: 空名删除错误提示")

    # 8.2 两表均未命中 -> eNo + not-found 契约消息
    r, out = capture(lambda: raw_logger("/YomkLogger/delete_logger", "lg1_s8_never_created"))
    check(r.status == ResStatus.eNo and r.msg == "logger not found.",
          "S8: 删除未注册 logger 返回 eNo + logger not found.")
    check("logger: lg1_s8_never_created not found." in out, "S8: 未命中错误提示含 logger 名")

    # 8.3 console-only：msg 计数 1/0；删除后单查 eNo；同名再写惰性重建
    name = "lg1_s8_console_only"
    r, _ = capture(lambda: log().tag(name).info("lg1_s8_console_marker"))
    check(r == ResStatus.eOk, "S8: console-only 预热创建 eOk")
    check(log_mgr().console(name).get_info().status ==
          ResStatus.eOk, "S8: 创建后单查命中 eOk")
    r, _ = capture(lambda: raw_logger("/YomkLogger/delete_logger", name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:1 file:0",
          f"S8: console-only 删除 eOk + msg 计数: {r.msg}")
    check(log_mgr().console(name).get_info().status ==
          ResStatus.eNo, "S8: console 删除后单查 eNo")
    # 惰性重建：同名再写一条 -> consoleLog 双检锁重建路径
    r, _ = capture(lambda: log().tag(name).info("lg1_s8_rebuild_marker"))
    check(r == ResStatus.eOk, "S8: 删除后同名再写 eOk（惰性重建）")
    check(log_mgr().console(name).get_info().status == ResStatus.eOk,
          "S8: 惰性重建后单查恢复 eOk")
    check(log_mgr().file(name).remove() ==
          ResStatus.eOk, "S8: 清理重建 logger eOk")

    # 8.4 file-only：写 2 行不 flush -> 删除释放最后引用 -> __del__ 锁外自动落盘
    dir84 = os.path.join(tmp_dir, "s8_file")
    os.makedirs(dir84, exist_ok=True)
    name = "lg1_s8_file_only"
    check(log_mgr().dir(dir84).file(name).create() ==
          ResStatus.eOk, "S8: file logger 创建 eOk")
    check(log().to_sink(name).tag("s8Tag").info("lg1_s8_file_marker_1") == ResStatus.eOk,
          "S8: file 写入第 1 行 eOk")
    check(log().to_sink(name).tag("s8Tag").info("lg1_s8_file_marker_2") == ResStatus.eOk,
          "S8: file 写入第 2 行 eOk")
    # 前置对照：未 flush 前 init 建的空文件仍为空
    content_before = read_file(os.path.join(dir84, name + ".log"))
    check(content_before == "", "S8: 删除前未 flush，文件仍为空")
    r, _ = capture(lambda: raw_logger("/YomkLogger/delete_logger", name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:0 file:1",
          f"S8: file-only 删除 eOk + msg 计数: {r.msg}")
    content = read_file(os.path.join(dir84, name + ".log"))
    check(count_lines(content) == 2, "S8: 删除触发析构自动落盘恰 2 行")
    check("lg1_s8_file_marker_1" in content and "lg1_s8_file_marker_2" in content,
          "S8: 落盘内容含两条 marker（数据不因删除丢失）")
    check(log_mgr().file(name).flush() ==
          ResStatus.eNo, "S8: 删除后 WRITE 同名返回 eNo")

    # 8.5 同名双表：一次删除同时清理两表（msg 计数 1/1），重复删除幂等 eNo
    dir85 = os.path.join(tmp_dir, "s8_both")
    os.makedirs(dir85, exist_ok=True)
    name = "lg1_s8_both_tables"
    r, _ = capture(lambda: log().tag(name).info("lg1_s8_both_console_marker"))
    check(r == ResStatus.eOk, "S8: 双表用例 console 侧创建 eOk")
    check(log_mgr().dir(dir85).file(name).create() ==
          ResStatus.eOk, "S8: 双表用例 file 侧创建 eOk")
    r, _ = capture(lambda: raw_logger("/YomkLogger/delete_logger", name))
    check(r.status == ResStatus.eOk and r.msg == "deleted console:1 file:1",
          f"S8: 同名双表一次删除 eOk + msg 计数: {r.msg}")
    check(log_mgr().console(name).get_info().status == ResStatus.eNo and
          log_mgr().file(name).get_info().status == ResStatus.eNo,
          "S8: 双表删除后按类型单查均 eNo（两表均已清理）")
    r, out2 = capture(lambda: raw_logger("/YomkLogger/delete_logger", name))
    check(r.status == ResStatus.eNo and r.msg == "logger not found.",
          "S8: 重复删除返回 eNo（幂等）")
    check("logger: lg1_s8_both_tables not found." in out2,
          "S8: 重复删除错误提示含 logger 名")

    # 8.6 内省行数递减守恒 + ALL 与 LOGGERS 行数关系 + main 删除后惰性重建
    dir86 = os.path.join(tmp_dir, "s8_count")
    os.makedirs(dir86, exist_ok=True)
    name = "lg1_s8_count"

    def lg_total():
        # 内省总行数 = console + file 两类型端点之和（按类型纯名清单）
        return (len(log_mgr().console().list().data) +
                len(log_mgr().file().list().data))

    before = lg_total()
    r, _ = capture(lambda: log().tag(name).info("lg1_s8_count_marker"))
    check(r == ResStatus.eOk, "S8: 计数用例 console 侧创建 eOk")
    check(log_mgr().dir(dir86).file(name).create() == ResStatus.eOk,
          "S8: 计数用例 file 侧创建 eOk")
    check(lg_total() == before + 2,
          "S8: 双表各建一个 -> LOGGERS 行数 +2")
    check(log_mgr().file(name).remove() ==
          ResStatus.eOk, "S8: 计数用例删除 eOk")
    check(lg_total() == before,
          "S8: 删除后 LOGGERS 行数回落（递减守恒）")
    all_info = log_mgr().all().data
    c = first_console_state(all_info)
    check(len(all_info) == before and
          c and c.debug_on and c.info_on and c.warn_on and c.error_on and c.proxy_on,
          "S8: ALL 清单条数 == LOGGERS 行数，console 条目状态段全 on 不变")

    # main 无特殊保护：可删；重建须走"空 logger 名回退"路径
    check(log_mgr().file("main").remove() == ResStatus.eOk,
          "S8: main 可删除 eOk（无特殊保护）")
    check(log_mgr().console("main").get_info().status == ResStatus.eNo,
          "S8: main 删除后单查 eNo")
    r, rebuild_out = capture(lambda: raw_logger(
        "/YomkLogger/console_log", Log(Log.eInfo, "lg1_s8_mainlogger_rebuild_marker", "")))
    check(r.status == ResStatus.eOk, "S8: main 删除后空名回退写入 eOk")
    check(log_mgr().console("main").get_info().status == ResStatus.eOk,
          "S8: main 惰性重建后单查恢复 eOk")
    check("[main] lg1_s8_mainlogger_rebuild_marker" in rebuild_out,
          "S8: 重建对象名确为 main（输出行 [main]）")
    check(lg_total() == before,
          "S8: main 删除+重建后行数回到基线（无泄漏无重复）")

    # 8.7 异常路径：空包 / 错类型包（YomkUnPackPkgResponse 契约分支）
    r_null = YomkApi.server().url("/YomkLogger/delete_logger").request()
    check(r_null.status == ResStatus.eNo and r_null.msg == "pkg is null or pkg is not String",
          "S8: delete_logger 空包返回 eNo + 契约消息")
    r_wrong = raw_logger(
        "/YomkLogger/delete_logger", Log(Log.eInfo, "x", "y"))
    check(r_wrong.status == ResStatus.eNo and r_wrong.msg == "pkg is null or pkg is not String",
          "S8: delete_logger 错类型包（Log）返回 eNo + 契约消息")

    # ============ Section 9: 服务级具名开关与全局态端点（两级主闸并联） ============
    tag_a = "lg1_s9_tagA"
    tag_b = "lg1_s9_tagB"

    # 9.1 具名 OFF 隔离：关 tagA 的 info，tagB 输出不受影响
    r_pre = capture(lambda: (
        log().tag(tag_a).info("lg1_s9a_pre_a_marker"),
        log().tag(tag_b).info("lg1_s9a_pre_b_marker")))[0]
    check(r_pre[0] == ResStatus.eOk and r_pre[1] == ResStatus.eOk,
          "S9a: tagA/tagB 预写 eOk")
    check(log_mgr().console(tag_a).level(Log.eInfo).disable() == ResStatus.eOk,
          "S9a: 具名 OFF_CONSOLE_LOG_INFO(tagA) 返回 eOk")
    r_a, out_a = capture(
        lambda: log().tag(tag_a).info("lg1_s9a_off_a_marker"))
    r_b, out_b = capture(
        lambda: log().tag(tag_b).info("lg1_s9a_off_b_marker"))
    check(r_a == ResStatus.eOk and r_b == ResStatus.eOk,
          "S9a: 具名关断后 tagA/tagB 调用仍 eOk")
    check("lg1_s9a_off_a_marker" not in out_a, "S9a: 具名关断后 tagA 无输出")
    check("lg1_s9a_off_b_marker" in out_b,
          "S9a: 具名关断不波及 tagB（逐器隔离）")

    # 9.2 具名 ON 恢复
    check(log_mgr().console(tag_a).level(Log.eInfo).enable() == ResStatus.eOk,
          "S9b: 具名 ON_CONSOLE_LOG_INFO(tagA) 返回 eOk")
    r, out = capture(
        lambda: log().tag(tag_a).info("lg1_s9b_on_a_marker"))
    check(r == ResStatus.eOk, "S9b: 恢复后 tagA 调用 eOk")
    check("lg1_s9b_on_a_marker" in out, "S9b: 具名恢复后 tagA 输出回归")

    # 9.3 具名未知名 eNo（不惰性创建）
    off_no = log_mgr().console("lg1_s9_never_created").level(Log.eInfo).disable()
    check(off_no == ResStatus.eNo and
          raw_logger("/YomkLogger/off_console_log_by_level",
                     Log(Log.eInfo, "", "lg1_s9_never_created")).msg
          == "console logger not found.",
          "S9c: 具名 OFF 未知名返回 eNo + console logger not found.")
    on_no = log_mgr().console("lg1_s9_never_created").level(Log.eInfo).enable()
    check(on_no == ResStatus.eNo,
          "S9c: 具名 ON 未知名返回 eNo")
    check(log_mgr().console("lg1_s9_never_created").get_info().status == ResStatus.eNo,
        "S9c: 未知名未被惰性创建（单查 eNo）")

    # 9.4 主闸×逐器组合 + 两层归因：全局 OFF + 逐器 ON → 仍无输出（两层 AND），
    # 全局态与条目逐器态各自真实呈现、归因唯一
    check(log_mgr().console().level(Log.eInfo).disable() == ResStatus.eOk,
          "S9d: 无参 OFF（全局主闸）返回 eOk")
    r, out = capture(lambda: log().tag(tag_a).info("lg1_s9d_global_off_a_marker"))
    check(r == ResStatus.eOk, "S9d: 主闸关断后 tagA 调用仍 eOk")
    check("lg1_s9d_global_off_a_marker" not in out,
          "S9d: 主闸 OFF + 逐器 ON → 仍无输出（两层 AND）")
    check(log_mgr().console().status().data.info_on is False,
          "S9d: logger_info_global 报主闸 info_off")
    li = log_mgr().console(tag_a).get_info()
    check(li.data is not None and li.data.logger_state.info_on,
          "S9d: 条目报逐器 info_on（与主闸态相反，两层归因唯一）")
    check(log_mgr().console().level(Log.eInfo).enable() == ResStatus.eOk,
          "S9d: 无参 ON（全局主闸）恢复 eOk")
    r, out = capture(lambda: log().tag(tag_a).info("lg1_s9d_global_on_a_marker"))
    check(r == ResStatus.eOk, "S9d: 主闸恢复后 tagA 调用 eOk")
    check("lg1_s9d_global_on_a_marker" in out,
          "S9d: 主闸恢复后输出回归（逐器仍 on）")

    # 9.5 条目逐器真值与两层正交：具名关断直接反映在条目、不波及全局态
    la = log_mgr().console(tag_a).get_info()
    lb = log_mgr().console(tag_b).get_info()
    check(la.data is not None and la.data.logger_state.info_on,
          "S9e: tagA 条目 info_on（具名恢复后逐器态）")
    check(lb.data is not None and lb.data.logger_state.info_on,
          "S9e: tagB 条目 info_on（未被具名触碰恒 true）")
    check(log_mgr().console(tag_a).level(Log.eInfo).disable() == ResStatus.eOk,
          "S9e: 再次具名关断 tagA 返回 eOk")
    check(log_mgr().console().status().data.info_on,
          "S9e: 具名关断不影响全局主闸（两层正交）")
    la2 = log_mgr().console(tag_a).get_info()
    check(la2.data is not None and not la2.data.logger_state.info_on,
          "S9e: tagA 条目报逐器 info_off（具名关断直接反映）")
    check(log_mgr().console(tag_a).level(Log.eInfo).enable() == ResStatus.eOk,
          "S9e: 具名恢复 tagA 返回 eOk（状态复原）")

    # 9.6 全局态端点初始值：global_state 端点解 LoggerState + 无参翻转联动
    g = log_mgr().console().status()
    check(g.status == ResStatus.eOk and g.msg == "ok",
          "S9f: logger_info_global 返回 eOk + ok")
    check(g.data is not None and g.data.debug_on and g.data.info_on
          and g.data.warn_on and g.data.error_on,
          "S9f: 全局主闸四级全 on")
    check(g.data is not None and g.data.proxy_on,
          "S9f: 全局态含 S3 已设 proxy_on")
    check(log_mgr().console().level(Log.eDebug).disable() == ResStatus.eOk,
          "S9f: 无参 OFF_DEBUG 返回 eOk")
    check(not log_mgr().console().status().data.debug_on,
          "S9f: 主闸 OFF_DEBUG 后全局态 debug_off")
    check(log_mgr().console().level(Log.eDebug).enable() == ResStatus.eOk,
          "S9f: 无参 ON_DEBUG 恢复 eOk")
    check(log_mgr().console().status().data.debug_on,
          "S9f: 主闸 ON_DEBUG 后全局态 debug_on")

    # ============ Section 10: 文件日志两级开关与全局态端点（与控制台同构） ============
    fl_name = "lg1_s10_fl"
    s10_dir = os.path.join(tmp_dir, "s10")
    os.makedirs(s10_dir, exist_ok=True)
    s10_log_file = os.path.join(s10_dir, fl_name + ".log")
    check(log_mgr().dir(s10_dir).file(fl_name).create() == ResStatus.eOk,
          "S10: 建文件日志器 lg1_s10_fl 成功")

    # 10.1 具名 OFF 隔离：关 lg1_s10_fl 的 info，warn 落盘不受影响
    check(log_mgr().file(fl_name).level(Log.eInfo).disable() == ResStatus.eOk,
          "S10a: 具名 OFF_FILE_LOG_INFO(fl) 返回 eOk")
    li = log_mgr().file(fl_name).get_info()
    check(li.data is not None and not li.data.logger_state.info_on
          and li.data.logger_state.warn_on,
          "S10a: 单查条目 info_off 且 warn_on（文件逐器门控真值）")
    check(log().to_sink(fl_name).tag("lg1s10").info("lg1_s10a_off_marker")
          == ResStatus.eOk, "S10a: 关断后 FILE_INFO 仍 eOk")
    check(log().to_sink(fl_name).tag("lg1s10").warn("lg1_s10a_warn_marker")
          == ResStatus.eOk, "S10a: WARN 写入 eOk")
    check(log_mgr().file(fl_name).flush() ==
          ResStatus.eOk, "S10a: WRITE eOk")
    s10_content = read_file(s10_log_file)
    check("lg1_s10a_off_marker" not in s10_content, "S10a: 关断级别不落盘（marker 缺席）")
    check("lg1_s10a_warn_marker" in s10_content, "S10a: 未关断级别落盘不受影响")

    # 10.2 具名 ON 恢复
    check(log_mgr().file(fl_name).level(Log.eInfo).enable() == ResStatus.eOk,
          "S10b: 具名 ON_FILE_LOG_INFO(fl) 返回 eOk")
    li = log_mgr().file(fl_name).get_info()
    check(li.data is not None and li.data.logger_state.info_on,
          "S10b: 条目 info_on 恢复")
    check(log().to_sink(fl_name).tag("lg1s10").info("lg1_s10b_on_marker")
          == ResStatus.eOk, "S10b: 恢复后 FILE_INFO eOk")
    check(log_mgr().file(fl_name).flush() ==
          ResStatus.eOk, "S10b: WRITE eOk")
    s10_content = read_file(s10_log_file)
    check("lg1_s10b_on_marker" in s10_content, "S10b: 恢复后落盘回归")

    # 10.3 具名未知名 eNo（不惰性创建）
    off_no = log_mgr().file("lg1_s10_never_created").level(Log.eInfo).disable()
    check(off_no == ResStatus.eNo
          and raw_logger("/YomkLogger/off_file_log_by_level",
                         Log(Log.eInfo, "", "lg1_s10_never_created")).msg
          == "file logger not found.",
          "S10c: 具名 OFF 未知名返回 eNo + file logger not found.")
    on_no = log_mgr().file("lg1_s10_never_created").level(Log.eInfo).enable()
    check(on_no == ResStatus.eNo,
          "S10c: 具名 ON 未知名返回 eNo")
    check("lg1_s10_never_created" not in log_mgr().file().list().data,
          "S10c: 未知名未被惰性创建（LOGGERS 无该名）")

    # 10.4 主闸×逐器组合 + 两层归因：文件主闸 OFF + 逐器 ON → 仍不落盘（两层 AND），
    # 文件全局态与条目逐器态各自真实呈现、归因唯一
    check(log_mgr().file().level(Log.eInfo).disable() == ResStatus.eOk,
          "S10d: 无参 OFF（文件主闸）返回 eOk")
    g = log_mgr().file().status()
    check(g.data is not None and not g.data.info_on,
          "S10d: logger_info_global(\"file\") 报文件主闸 info_off")
    li = log_mgr().file(fl_name).get_info()
    check(li.data is not None and li.data.logger_state.info_on,
          "S10d: 条目报逐器 info_on（与文件主闸态相反，两层归因唯一）")
    check(log().to_sink(fl_name).tag("lg1s10").info("lg1_s10d_gate_marker")
          == ResStatus.eOk, "S10d: 主闸关断后 FILE_INFO 仍 eOk")
    check(log_mgr().file(fl_name).flush() ==
          ResStatus.eOk, "S10d: WRITE eOk")
    s10_content = read_file(s10_log_file)
    check("lg1_s10d_gate_marker" not in s10_content,
          "S10d: 主闸 OFF + 逐器 ON → 仍不落盘（两层 AND）")
    check(log_mgr().file().level(Log.eInfo).enable() == ResStatus.eOk,
          "S10d: 无参 ON（文件主闸）恢复 eOk")
    check(log().to_sink(fl_name).tag("lg1s10").info("lg1_s10d_on_marker")
          == ResStatus.eOk, "S10d: 主闸恢复后 FILE_INFO eOk")
    check(log_mgr().file(fl_name).flush() ==
          ResStatus.eOk, "S10d: WRITE eOk")
    s10_content = read_file(s10_log_file)
    check("lg1_s10d_on_marker" in s10_content, "S10d: 主闸恢复后落盘回归（逐器仍 on）")

    # 10.5 两层正交：具名关断直接反映在条目、不波及文件主闸态
    check(log_mgr().file(fl_name).level(Log.eInfo).disable() == ResStatus.eOk,
          "S10e: 再次具名关断返回 eOk")
    g = log_mgr().file().status()
    check(g.data is not None and g.data.info_on,
          "S10e: 具名关断不影响文件主闸（两层正交）")
    check(log_mgr().file(fl_name).level(Log.eInfo).enable() == ResStatus.eOk,
          "S10e: 具名恢复 eOk（状态复原）")

    # 10.6 全局态端点初始值：console·file global_state 两套主闸独立 + 无参翻转联动
    g = log_mgr().file().status()
    check(g.status == ResStatus.eOk and g.msg == "ok",
          "S10f: logger_info_global(\"file\") 返回 eOk + ok")
    check(g.data is not None and g.data.debug_on and g.data.info_on
          and g.data.warn_on and g.data.error_on,
          "S10f: 文件主闸四级全 on")
    check(g.data is not None and not g.data.proxy_on,
          "S10f: 文件态 proxy_on=False（文件代理未设置时恒 false）")
    check(log_mgr().file().level(Log.eDebug).disable() == ResStatus.eOk,
          "S10f: 无参 OFF_FILE_LOG_DEBUG 返回 eOk")
    check(not log_mgr().file().status().data.debug_on,
          "S10f: 文件主闸 OFF_DEBUG 后文件态 debug_off")
    # 两套主闸独立直证：文件主闸翻转不波及控制台主闸
    gc = log_mgr().console().status()
    check(gc.data is not None and gc.data.debug_on and gc.data.info_on
          and gc.data.warn_on and gc.data.error_on,
          "S10f: 控制台主闸四级仍全 on（与文件主闸独立）")
    check(log_mgr().file().level(Log.eDebug).enable() == ResStatus.eOk,
          "S10f: 无参 ON_FILE_LOG_DEBUG 恢复 eOk")
    check(log_mgr().file().status().data.debug_on,
          "S10f: 文件主闸 ON_DEBUG 后文件态 debug_on")
    # 未知 type 客户端拦截属已删除的扁平 logger_info_global 面；六域链式入口
    # console()/file() 天然只收敛两型，无未知 type 形态可测（契约随扁平面退场）

    # ============ Section 11: 文件日志全局代理契约（与控制台 S3 同构） ============
    fl_name = "lg1_s11_fl"
    s11_dir = os.path.join(tmp_dir, "s11")
    os.makedirs(s11_dir, exist_ok=True)
    s11_log_file = os.path.join(s11_dir, fl_name + ".log")
    check(log_mgr().dir(s11_dir).file(fl_name).create() == ResStatus.eOk,
          "S11: 建文件日志器 lg1_s11_fl 成功")
    file_proxy_hits = []
    file_proxy_state = {"intercept": True}

    def file_proxy(log_pkg):
        file_proxy_hits.append(1)
        return not file_proxy_state["intercept"]

    # 11.1 起点：两套代理独立直证（文件代理未设置 + 控制台代理 S3 重装态）
    gf = log_mgr().file().status()
    gc = log_mgr().console().status()
    check(gf.status == ResStatus.eOk and gc.status == ResStatus.eOk
          and gf.data is not None and gc.data is not None,
          "S11a: GLOBAL(file/console) 均返回 eOk")
    check(not gf.data.proxy_on and gc.data.proxy_on,
          "S11a: 文件 proxy_off + 控制台 proxy_on（两套代理各自独立直证）")

    # 11.2 拦截态：proxy 返回 False -> eOk + 专属消息 + marker 缺席
    check(log_mgr().file().proxy(file_proxy).apply() == ResStatus.eOk,
          "S11b: SET_FILE_LOG_PROXY 返回 eOk")
    r = raw_logger("/YomkLogger/file_log", Log(Log.eInfo, "lg1_s11b_intercept_marker", fl_name, "lg1s11"))
    check(r.status == ResStatus.eOk and r.msg == "file log proxy is success.",
          "S11b: 拦截态 file_log 返回 eOk + 专属契约消息")
    check(log_mgr().file(fl_name).flush() == ResStatus.eOk,
          "S11b: WRITE eOk")
    check("lg1_s11b_intercept_marker" not in read_file(s11_log_file),
          "S11b: 拦截态条目不落盘（marker 缺席）")
    check(len(file_proxy_hits) == 1, "S11b: 文件代理被调用 1 次（原子计数）")

    # 11.3 穿透态：proxy 返回 True -> 走默认落盘
    file_proxy_state["intercept"] = False
    r = raw_logger("/YomkLogger/file_log", Log(Log.eInfo, "lg1_s11c_pass_marker", fl_name, "lg1s11"))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "S11c: 穿透态 file_log 返回 eOk + success.")
    check(log_mgr().file(fl_name).flush() == ResStatus.eOk,
          "S11c: WRITE eOk")
    check("lg1_s11c_pass_marker" in read_file(s11_log_file),
          "S11c: 穿透态条目落盘回归")
    check(len(file_proxy_hits) == 2, "S11c: 文件代理累计被调用 2 次")
    check(log_mgr().file().status().data.proxy_on,
          "S11c: GLOBAL(file) 状态段 proxy_on")

    # 11.4 卸载（镜像控制台卸载流程）：卸载前先切回拦截态，确认代理确实在链路上
    file_proxy_state["intercept"] = True
    r = raw_logger("/YomkLogger/file_log", Log(Log.eInfo, "lg1_s11d_pre_unload_marker", fl_name, "lg1s11"))
    check(r.status == ResStatus.eOk and r.msg == "file log proxy is success.",
          "S11d: 卸载前拦截态契约消息（代理确实在链路上）")
    check(log_mgr().file().proxy(None).apply() == ResStatus.eOk,
          "S11d: set_file_log_proxy(None) 卸载返回 eOk")
    check(not log_mgr().file().status().data.proxy_on,
          "S11d: 卸载后 GLOBAL(file) 回到 proxy_off")
    hits_before = len(file_proxy_hits)
    r = raw_logger("/YomkLogger/file_log", Log(Log.eInfo, "lg1_s11d_post_unload_marker", fl_name, "lg1s11"))
    check(r.status == ResStatus.eOk and r.msg == "success.",
          "S11d: 卸载后 file_log 返回 eOk + success.（走默认路径而非代理短路）")
    check(log_mgr().file(fl_name).flush() == ResStatus.eOk,
          "S11d: WRITE eOk")
    check("lg1_s11d_post_unload_marker" in read_file(s11_log_file),
          "S11d: 卸载后条目落盘回归（默认路径）")
    check(len(file_proxy_hits) == hits_before,
          "S11d: 卸载后代理不再被调用（命中数不增）")

    # 11.5 复位：重装同一代理并回到穿透态，维持不变量
    file_proxy_state["intercept"] = False
    check(log_mgr().file().proxy(file_proxy).apply() == ResStatus.eOk,
          "S11e: 重装文件代理返回 eOk（卸载可逆）")
    check(log_mgr().file().status().data.proxy_on,
          "S11e: 重装后 GLOBAL(file) 恢复 proxy_on（不变量已复位）")

    # ============ 收尾清理 ============
    YomkApi.server().shutdown()
    shutil.rmtree(tmp_dir, ignore_errors=True)

    if g_failed == 0:
        print("TestYomkLoggerLifecycle all check passed.")
        return 0
    print(f"TestYomkLoggerLifecycle FAILED ({g_failed} checks failed).")
    return 1

if __name__ == "__main__":
    sys.exit(main())
