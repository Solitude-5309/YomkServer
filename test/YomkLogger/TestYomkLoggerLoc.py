#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerLoc.py —— 日志位置前缀（source location）链式测试
（4 节）

覆盖内容：
1. S1 默认配置行号精确性：`log()` 入口捕获调用行 (file,line,func)，六级别 +
   tag 变体全 Succeeded，行号与发射行严格一致（inspect.currentframe 取
   调用行）；双链一致性（缺省链 vs tag("main") 链归一化后逐字节相等）；
   零 body（仅前缀成行）与多参折叠（"i= 42 j= 3.5 end"）精确断言
2. S2 文件落盘读回：log_mgr().dir().file().create() → to_sink 六级 + tag 变体
   发射 Succeeded → flush() → 读 <dir>/lgL_s2.log 断言各级别行（[N] 前缀在内容
   头部）→ remove()（不删磁盘 .log，随临时目录清理）
3. S3 位置开关 8 组合矩阵：逐组 location().show_file/l/func().apply()，
   控制台行 fullmatch 期望前缀（file basename / 行号 / main 三要素组合）；
   收尾恢复默认（file=关/line=开/func=关）并实测一行
4. S4 tag 链直调隔离 + 类型折叠护栏：tag("lgL_s4") 四级 Succeeded + marker；
   变参 True/False/int/大整数/float/自定义 str 折叠为单空格串

实现注记：
- 全关组合为单空格口径（`_chain_log_content` 空前缀不加导空格）；
- 变参折叠以 bool/int/float/自定义类（str 折叠）承载；
- 矩阵组合与文件落盘的期望串用正则 \\d+ 承载行号（S1 已单独证明行号精确性）。

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

from yomk.msg.server import ResStatus
from yomk.server import server
from yomk.log import log, log_mgr

g_failed = 0
TIME_RE = re.compile(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]")
SELF_BASE = "TestYomkLoggerLoc.py"


def console_line_re(level, tag, content_re):
    # 控制台整行正则：时间戳 + [LEVEL] + [tag] + 内容正则（字面段 re.escape 承载）
    return TIME_RE.pattern + " " + re.escape(f"[{level}] [{tag}] ") + content_re


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def test_tmp_root():
    # 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退系统临时目录
    return os.environ.get("YOMK_TEST_TMP_ROOT") or tempfile.gettempdir()


def last_line(buf):
    lines = [ln for ln in buf.getvalue().splitlines() if ln]
    return lines[-1] if lines else ""


def normalize_line(ln, lineno):
    # 双链归一化：时间戳 → [TS]，本行行号 → [N]，其余逐字节保留
    ln = TIME_RE.sub("[TS]", ln, count=1)
    return ln.replace(f"[{lineno}]", "[N]")


def main():
    Succeeded = ResStatus.Succeeded
    tmp_dir = os.path.join(test_tmp_root(), "yomk_logger_loc_" + str(os.getpid()))

    # 初始化框架（发射链依赖已 init 的服务路由：server().async_thread(1).init()）
    server().async_thread(1).init()

    # ============ S1 默认配置：行号精确性 / 双链一致性 / 折叠 ============
    # 六级别（trace..fatal）默认 tag=main；want 指向 3 行之后的发射行
    level_expect = {
        "trace": ("TRACE", "lgL_s1_trace"),
        "debug": ("DEBUG", "lgL_s1_debug"),
        "info": ("INFO ", "lgL_s1_info"),
        "warn": ("WARN ", "lgL_s1_warn"),
        "error": ("ERROR", "lgL_s1_error"),
        "fatal": ("FATAL", "lgL_s1_fatal"),
    }
    s1_ok = True
    for meth, (label, marker) in level_expect.items():
        want = sys._getframe().f_lineno + 3
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ret = getattr(log(), meth)(marker)
        ln = last_line(buf)
        if ret != Succeeded or not ln.endswith(
                f"[{label}] [main] [{want}] {marker}"):
            s1_ok = False
            print(f"       S1 {meth} 实际行: {ln!r} 期望尾部: ...[{label}] [main] [{want}] {marker}")
    check(s1_ok, "S1: 六级别 Succeeded + [tag] [行号] marker 精确（行号=发射行）")

    want = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ret = log().tag("lgLTag").info("lgL_s1_tagged")
    ln_tag = last_line(buf)
    check(ret == Succeeded and ln_tag.endswith(f"[INFO ] [lgLTag] [{want}] lgL_s1_tagged"),
          f"S1: tag 变体 [lgLTag] 精确（实际: {ln_tag!r}）")

    # 双链一致性：缺省链 vs 显式 tag("main") 链，归一化时间戳/行号后逐字节相等
    want_a = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        log().info("lgL_s1_dualchain")
    line_a = last_line(buf)
    want_b = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        log().tag("main").info("lgL_s1_dualchain")
    line_b = last_line(buf)
    check(normalize_line(line_a, want_a) == normalize_line(line_b, want_b),
          f"S1: 双链一致性（归一化后逐字节相等）\n       a: {line_a!r}\n       b: {line_b!r}")

    # 零 body：内容仅剩位置前缀成行
    want = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ret = log().info()
    zero_re = re.compile(console_line_re("INFO ", "main",
                                         re.escape(f"[{want}]") + r"$"))
    check(ret == Succeeded and bool(zero_re.fullmatch(last_line(buf))),
          f"S1: 零 body 仅前缀成行（实际: {last_line(buf)!r}）")

    # 多参折叠：单空格拼接 "i= 42 j= 3.5 end"
    want = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ret = log().info("i=", 42, "j=", 3.5, "end")
    check(ret == Succeeded and
          last_line(buf).endswith(f"[{want}] i= 42 j= 3.5 end"),
          f"S1: 多参折叠单空格精确（实际: {last_line(buf)!r}）")

    # ============ S2 文件日志器落盘读回 ============
    os.makedirs(tmp_dir, exist_ok=True)
    ret = log_mgr().dir(tmp_dir).file("lgL_s2").create()
    check(ret == Succeeded, f"S2: file create Succeeded（实际 {ret}）")
    sink_ok = True
    for meth in ("trace", "debug", "info", "warn", "error", "fatal"):
        buf = io.StringIO()  # to_sink 不经控制台，仅消费状态
        with contextlib.redirect_stdout(buf):
            r = getattr(log().to_sink("lgL_s2"), meth)(f"lgL_s2_{meth}")
        if r != Succeeded:
            sink_ok = False
    ret_sink_tag = log().to_sink("lgL_s2").tag("lgL_s2_tag").info("lgL_s2_tagged")
    check(sink_ok, "S2: to_sink 六级发射全 Succeeded")
    check(ret_sink_tag == Succeeded, "S2: to_sink tag 变体 Succeeded")

    ret = log_mgr().file("lgL_s2").flush()
    log_path = os.path.join(tmp_dir, "lgL_s2.log")
    check(ret == Succeeded and os.path.isfile(log_path),
          f"S2: flush Succeeded 且日志文件生成 {log_path}")
    content = ""
    if os.path.isfile(log_path):
        with open(log_path) as f:
            content = f.read()
    file_ok = True
    for meth in ("trace", "debug", "info", "warn", "error", "fatal"):
        label = {"trace": r"TRACE", "debug": r"DEBUG", "info": r"INFO\ ",
                 "warn": r"WARN\ ", "error": r"ERROR", "fatal": r"FATAL"}[meth]
        if not re.search(r"\[" + label + r"\] \[main\] \[\d+\] lgL_s2_" + meth, content):
            file_ok = False
            print(f"       S2 缺失落盘行: {meth}")
    check(file_ok, "S2: 六级落盘行格式（[LEVEL] [main] [N] marker）读回命中")
    check(re.search(r"\[INFO \] \[lgL_s2_tag\] \[\d+\] lgL_s2_tagged", content),
          "S2: tag 变段落盘（[lgL_s2_tag]）")

    ret = log_mgr().file("lgL_s2").remove()
    check(ret == Succeeded, "S2: remove Succeeded（注：不删磁盘 .log，随临时目录清理）")

    # ============ S3 位置开关 8 组合矩阵 ============
    # (show_file, show_line, show_func, marker 码, 期望内容正则) 组合表；
    # 全关组合为单空格口径（空前缀不加导空格）
    combos = [
        (False, True, False, "-l-", r"\[\d+\] lgL_s3_-l-"),
        (True, True, False, "fl-",
         r"\[" + SELF_BASE.replace(".", r"\.") + r":\d+\] lgL_s3_fl-"),
        (True, True, True, "fln",
         r"\[" + SELF_BASE.replace(".", r"\.") + r":\d+:main\] lgL_s3_fln"),
        (False, True, True, "-ln", r"\[\d+:main\] lgL_s3_-ln"),
        (True, False, False, "f--",
         r"\[" + SELF_BASE.replace(".", r"\.") + r"\] lgL_s3_f--"),
        (False, False, True, "--n", r"\[main\] lgL_s3_--n"),
        (True, False, True, "f-n",
         r"\[" + SELF_BASE.replace(".", r"\.") + r":main\] lgL_s3_f-n"),
        (False, False, False, "---", r"lgL_s3_---"),
    ]
    matrix_ok = True
    for sf, sl, sn, code, expect_re in combos:
        log_mgr().location().show_file(sf).show_line(sl).show_func(sn).apply()
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ret = log().info("lgL_s3_" + code)
        ln = last_line(buf)
        pattern = console_line_re("INFO ", "main", expect_re)
        if ret != Succeeded or not re.fullmatch(pattern, ln):
            matrix_ok = False
            print(f"       组合 {code} 实际: {ln!r} 期望: {pattern}")
    check(matrix_ok, "S3: 8 组合开关矩阵前缀精确（含 --n 与 tag main 撞名仍命中）")

    # 收尾恢复默认（file=关/line=开/func=关）并实测一行
    ret = log_mgr().location().show_file(False).show_line(True).show_func(False).apply()
    want = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        log().info("lgL_s3_restore")
    check(ret == Succeeded and last_line(buf).endswith(f"[{want}] lgL_s3_restore"),
          "开关矩阵收尾恢复默认 (file=关/line=开/func=关)")

    # ============ S4 tag 链直调隔离 + 类型折叠护栏 ============
    tag_ok = True
    for meth in ("debug", "info", "warn", "error"):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ret = getattr(log().tag("lgL_s4"), meth)("lgL_s4_" + meth)
        if ret != Succeeded or f"lgL_s4_{meth}" not in last_line(buf):
            tag_ok = False
    check(tag_ok, "S4: tag 链四级 Succeeded + marker 命中")

    class _Cust:
        def __str__(self):
            return "cust<7>"

    want = sys._getframe().f_lineno + 3
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ret = log().tag("lgL_s4").info(
            True, False, "a", -42, -2147483648, 18446744073709551615, 3.5, _Cust())
    check(ret == Succeeded and last_line(buf).endswith(
        f"[{want}] true false a -42 -2147483648 18446744073709551615 3.5 cust<7>"),
        f"S4: 类型折叠护栏精确（实际: {last_line(buf)!r}）")

    # ============ 收尾 ============
    shutil.rmtree(tmp_dir, ignore_errors=True)
    server().shutdown()

    if g_failed == 0:
        print("TestYomkLoggerLoc all check passed.")
        return 0
    print(f"TestYomkLoggerLoc FAILED ({g_failed} checks failed).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
