#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerConcurrency.py —— YomkLogger 模块多线程并发正确性验证（LG3，
5 节 / 72 断言）

覆盖内容（5 节）：
S1 并发 console_log 守恒 + 行完整性（只读命中路径，8x200 同 tag 预热后并发，验证
ConsoleLogger m_mutex 整行互斥）；S2 并发新 tag 自动创建（P1-a 双检锁核心：S2a 8
线程屏障对齐争用同一新 tag / S2b 8 线程分散唯一 tag / S2c P3-b 死分支登记）；S3
并发 proxy set x console_log x 内省（P1-b 专属锁，6 caller + 2 内省 + 50 轮交替
SET v1/v2，v1+v2 == log 总数守恒）；S4 并发 file logger 创建 x 写入 x 落盘（S4a
同名争用恰一胜 / S4b 异名全 eOk / S4c 8x250 + 2 writer 2000 行不丢不重 / S4d
4x10 轮多写者 WRITE 幂等）；S5 全量 churn + P3-a 内省原子快照判别（3x600=1800 对
成对创建，LOGGERS/ALL 快照 C>=F 违例 == 0）

语言机制说明：
- ThreadSafeCoutCapture -> contextlib.redirect_stdout(io.StringIO)：主线程先换
  sys.stdout 再 spawn/join（子线程调用期解析 sys.stdout 即得捕获缓冲）；
  StringIO.write 单次调用 GIL 原子 + ConsoleLogger m_mutex 整行单次 write，
  行完整性断言仅在"同一 logger"段做（设计边界：跨 logger 段只断言 marker
  计数守恒，python 统一捕获降噪；S3/S5 断言不涉及输出内容，不受影响）
- std::atomic 计数 -> list.append + len()（GIL 单步原子，免锁）；停止门
  g_s3Stop/g_s4cStop/g_s5Stop -> threading.Event；起始屏障 waitGate/releaseGate
  -> StartGate（Condition 版 ready 计数 + go 标志 + 10s 超时防挂死，语义逐字对应）
- std::mutex + vector 快照收集 -> 线程本地 list 写自身槽位，join 后主线程汇总
  （无跨线程共享写，免锁）
- 规模保真：S5 churn 3x600（1800 对）为 P3-a 判别式灵敏度前提，不下调；其余段规模
  8x200 / 8x50 / 6+2+50 / 8x250+2 / 4x10 / 2x100 / 2x200 / 2x200
- proxy v1/v2 -> 闭包函数对（恒返回 True 穿透态，不影响后续 Section），
  list.append 命中计数；SET 交替 50 轮写侧与 caller 读侧并发交错
- console_log_*/file_log_*_tag 门面统一形态：[行号] 注入在内容头部、marker 仍位于
  行尾，行尾 marker 守恒判定不受影响

设计边界（契约内使用，非缺陷）：
- 全程不调 /delete_logger（删除语义由 Lifecycle S8 覆盖）；proxy 安装后不卸载且
  v1/v2 恒穿透，不影响 S4/S5 输出与落盘断言；ALL 首行级别开关组合无原子保证，
  S5 仅断言格式合法与收尾精确串
"""

import contextlib
import io
import os
import re
import shutil
import sys
import tempfile
import threading
import time

import YomkApi
from YomkServerPy import ResStatus

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
    # 统计非空行数
    return sum(1 for ln in content.splitlines() if ln)


def read_file(path):
    with open(path) as f:
        return f.read()


def collect_tail_markers(text, prefix):
    # 行尾最后一段（末个空格之后）以 prefix 开头的 marker 出现次数
    # 用途：console 捕获文本 / file 落盘文本的 marker 守恒（不丢不重）判定
    markers = {}
    for line in text.splitlines():
        if not line:
            continue
        tail = line.rsplit(" ", 1)[-1]
        if tail.startswith(prefix):
            markers[tail] = markers.get(tail, 0) + 1
    return markers


def count_bad_occurrence(markers):
    # 统计 markers 中出现次数 != 1 的条目数（0 即"不丢不重"）
    return sum(1 for c in markers.values() if c != 1)


def count_lines_containing(lines, needle):
    return sum(1 for ln in lines if needle in ln)


def unpack_lines(resp):
    # 解包 LOGGERS/ALL 的 list[str]；失败返回空列表
    if resp.status != ResStatus.eOk or not resp.data:
        return []
    return list(resp.data)


def count_logger_lines(lines):
    # 统计 LOGGERS 行中的 console/file 段行数
    console_lines = 0
    file_lines = 0
    for line in lines:
        if line.endswith(" [console]"):
            console_lines += 1
            continue
        if " [file] dir:" in line:
            file_lines += 1
    return console_lines, file_lines


def check_level_line_format(line):
    # 校验 ALL 首行格式：console:debug:(on|off) info:(on|off) warn:(on|off)
    # error:(on|off) proxy:(on|off)
    pos = 0

    def match_field(key):
        nonlocal pos
        if not line.startswith(key, pos):
            return False
        pos += len(key)
        if line.startswith("on", pos):
            pos += 2
            return True
        if line.startswith("off", pos):
            pos += 3
            return True
        return False

    for key in ("console:debug:", "info:", "warn:", "error:"):
        if not match_field(key):
            return False
        if pos >= len(line) or line[pos] != " ":
            return False
        pos += 1
    if not match_field("proxy:"):
        return False
    return pos == len(line)


def is_legal_status(resp):
    # 状态码是否属于框架合法集合 {eInvalid, eOk, eNo}
    return resp.status in (ResStatus.eInvalid, ResStatus.eOk, ResStatus.eNo)


class StartGate:
    # 起始屏障
    def __init__(self):
        self._cv = threading.Condition()
        self._ready = 0
        self._go = False

    def wait(self):
        with self._cv:
            self._ready += 1
            while not self._go:
                self._cv.wait(timeout=0.05)

    def release(self, parties, timeout_s=10.0):
        deadline = time.monotonic() + timeout_s
        with self._cv:
            while self._ready < parties and time.monotonic() < deadline:
                self._cv.wait(timeout=0.01)
            self._go = True
            self._cv.notify_all()


def main():
    server = YomkApi.init(1)
    check(server is not None, "YomkApi.init 返回非空服务器")

    tmp_dir = os.path.join(tempfile.gettempdir(),
                           "yomk_logger_lg3_" + str(os.getpid()))
    os.makedirs(tmp_dir, exist_ok=True)
    check(os.path.isdir(tmp_dir), f"临时目录创建成功: {tmp_dir}")

    bad_status = []  # S4/S5 共用：非法状态码观测

    # ========================================================================
    # Section 1：并发 console_log 守恒 + 行完整性（只读命中路径）
    # ========================================================================
    k_threads = 8
    k_logs_per_thread = 200
    k_total = k_threads * k_logs_per_thread

    # 预热与循环体共用同一 tag 字面量，并发段全部走命中路径
    k_tag = "lg3_s1_shared"
    s1_ok_count = []

    # 预热：主线程先创建该 tag 的 console logger，使并发段全部走命中路径
    r, _ = capture(lambda: YomkApi.console_log_info_tag(k_tag, "lg3s1_warmup"))
    check(r.status == ResStatus.eOk, "S1: 预热创建 console logger " + k_tag)

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        gate = StartGate()
        workers = []
        for t in range(k_threads):
            def s1_worker(t=t):
                gate.wait()
                for i in range(k_logs_per_thread):
                    resp = YomkApi.console_log_info_tag(
                        k_tag, "lg3s1_t" + str(t) + "_i" + str(i))
                    if resp.status == ResStatus.eOk:
                        s1_ok_count.append(1)
            workers.append(threading.Thread(target=s1_worker))
        for th in workers:
            th.start()
        gate.release(k_threads)
        for th in workers:
            th.join()
    out = buf.getvalue()

    check(len(s1_ok_count) == k_total,
          f"S1: 响应守恒 eOk == {k_total}（实际 {len(s1_ok_count)}）")

    markers = collect_tail_markers(out, "lg3s1_t")
    check(len(markers) == k_total,
          f"S1: 唯一 marker 数 == {k_total}（实际 {len(markers)}）——并发输出不丢")
    check(count_bad_occurrence(markers) == 0,
          "S1: 全部 marker 恰出现 1 次（无重复、无撕裂）——ConsoleLogger m_mutex 整行互斥")

    # 行完整性：同一 logger 的每一行均满足时间戳 + 级别 + tag 前缀格式（无跨行撕裂）
    intact_lines = 0
    broken_lines = 0
    for line in out.splitlines():
        if "lg3s1_t" not in line:
            continue  # 框架自身噪声行不参与判定
        if check_time_format(line) and "[Info ] [" + k_tag + "]" in line:
            intact_lines += 1
        else:
            broken_lines += 1
    check(broken_lines == 0,
          f"S1: 含 marker 的行全部完整（时间戳+[Info ]+[tag] 格式，撕裂行 {broken_lines} 条）")
    check(intact_lines == k_total,
          f"S1: 完整行数 == {k_total}（实际 {intact_lines}）")

    # 同一 tag 仅一个 logger 实例（并发命中路径不重复创建）
    loggers_lines = unpack_lines(YomkApi.logger_info_loggers())
    check(count_lines_containing(loggers_lines, k_tag + " [console]") == 1,
          f"S1: LOGGERS 中 {k_tag} 恰 1 行（无重复实例）")

    # ========================================================================
    # Section 2：并发新 tag 自动创建（P1-a 双检锁核心验证）
    # ========================================================================
    # ---- S2a：最大争用——8 线程同时对同一全新 tag 打日志 ----
    k_total = k_threads * k_logs_per_thread
    k_race_tag = "lg3_s2_race"
    s2a_ok_count = []

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        gate = StartGate()
        workers = []
        for t in range(k_threads):
            def s2a_worker(t=t):
                gate.wait()  # 屏障对齐：8 线程同时 miss -> 同时升级独占锁
                for i in range(k_logs_per_thread):
                    resp = YomkApi.console_log_info_tag(
                        k_race_tag, "lg3s2a_t" + str(t) + "_i" + str(i))
                    if resp.status == ResStatus.eOk:
                        s2a_ok_count.append(1)
            workers.append(threading.Thread(target=s2a_worker))
        for th in workers:
            th.start()
        gate.release(k_threads)
        for th in workers:
            th.join()
    out = buf.getvalue()

    check(len(s2a_ok_count) == k_total,
          f"S2a: 争用创建全部 eOk == {k_total}（实际 {len(s2a_ok_count)}）")

    loggers_lines = unpack_lines(YomkApi.logger_info_loggers())
    check(count_lines_containing(loggers_lines, k_race_tag + " [console]") == 1,
          f"S2a: LOGGERS 中 {k_race_tag} 恰 1 行——双检锁下并发 miss 仅创建一个实例")

    race_markers = collect_tail_markers(out, "lg3s2a_t")
    check(len(race_markers) == k_total,
          f"S2a: 唯一 marker 数 == {k_total}（实际 {len(race_markers)}）——同一实例被并发复用无丢失")
    check(count_bad_occurrence(race_markers) == 0,
          "S2a: 全部 marker 恰出现 1 次（不丢不重）")

    race_info = YomkApi.logger_info_logger(k_race_tag)
    check(race_info.status == ResStatus.eOk and race_info.data is not None
          and race_info.data.type == "console",
          f"S2a: LOGGER({k_race_tag}) 命中 console 类型")

    # ---- S2b：分散创建——8 线程各自唯一 tag ----
    k_spread_threads = 8
    k_spread_per_thread = 50
    k_spread_total = k_spread_threads * k_spread_per_thread
    s2b_ok_count = []
    console_before, file_before = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))

    with contextlib.redirect_stdout(io.StringIO()):
        gate = StartGate()
        workers = []
        for t in range(k_spread_threads):
            def s2b_worker(t=t):
                tag = "lg3_s2_t" + str(t)
                gate.wait()
                for i in range(k_spread_per_thread):
                    resp = YomkApi.console_log_info_tag(
                        tag, "lg3s2b_t" + str(t) + "_i" + str(i))
                    if resp.status == ResStatus.eOk:
                        s2b_ok_count.append(1)
            workers.append(threading.Thread(target=s2b_worker))
        for th in workers:
            th.start()
        gate.release(k_spread_threads)
        for th in workers:
            th.join()

    check(len(s2b_ok_count) == k_spread_total,
          f"S2b: 分散创建全部 eOk == {k_spread_total}（实际 {len(s2b_ok_count)}）")

    console_after, file_after = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(console_after - console_before == k_spread_threads,
          f"S2b: console logger 新增恰 {k_spread_threads} 个（实际新增 {console_after - console_before}）")
    check(file_after == file_before, "S2b: file logger 数量未受影响")

    all_spread_hit = True
    for t in range(k_spread_threads):
        tag = "lg3_s2_t" + str(t)
        info = YomkApi.logger_info_logger(tag)
        if info.status != ResStatus.eOk or info.data is None or info.data.type != "console":
            all_spread_hit = False
    check(all_spread_hit, "S2b: 8 个唯一 tag 的 LOGGER 内省全部命中 console 类型")

    # ---- S2c：P3-b 判定登记（LG6 已收口）----
    # S2a（同一新 tag 8 线程争用）与 S2b（8 个新 tag 并发创建）是 consoleLog 中
    # 重名防御分支的唯一可能触达场景；双检锁在独占锁内已二次查找确认 key 不存在，
    # 本组 S2a/S2b 即该删除的行为学佐证（删前删后均全绿、无 eNo 返回）。
    print("[INFO] S2c: P3-b(!result.second) 最大争用场景已覆盖，该死分支已于 LG6 删除")

    # ========================================================================
    # Section 3：并发 proxy set × console_log × 内省（P1-b 专属锁核心验证）
    # ========================================================================
    k_caller_threads = 6
    k_set_rounds = 50
    k_introspect_threads = 2
    k_tag = "lg3_s3_proxy"

    s3_v1_hits = []
    s3_v2_hits = []
    s3_log_total = []
    s3_log_ok = []
    s3_set_ok = []
    s3_bad_level_line = []
    s3_stop = threading.Event()

    def s3_proxy_v1(_log_pkg):
        s3_v1_hits.append(1)
        return True  # 穿透：继续走默认控制台输出，不影响后续 Section

    def s3_proxy_v2(_log_pkg):
        s3_v2_hits.append(1)
        return True

    # 首次设置为 v1（此后进程内 proxy 恒为 on，v1/v2 交替更新 func）
    check(YomkApi.set_console_log_proxy(s3_proxy_v1).status == ResStatus.eOk,
          "S3: SET_CONSOLE_LOG_PROXY(v1) 返回 eOk")

    with contextlib.redirect_stdout(io.StringIO()):
        # caller 线程：持续 console_log 直到停止门
        callers = []
        for t in range(k_caller_threads):
            def caller_worker(t=t):
                i = 0
                while not s3_stop.is_set():
                    resp = YomkApi.console_log_info_tag(
                        k_tag, "lg3s3_t" + str(t) + "_i" + str(i))
                    s3_log_total.append(1)
                    if resp.status == ResStatus.eOk:
                        s3_log_ok.append(1)
                    i += 1
            callers.append(threading.Thread(target=caller_worker))

        # 内省线程：并发轮询 ALL，校验首行格式（proxy:on 恒定 + 无撕裂）
        introspectors = []
        for _t in range(k_introspect_threads):
            def s3_introspect_worker():
                while not s3_stop.is_set():
                    lines = unpack_lines(YomkApi.logger_info_all())
                    if lines:
                        if (not check_level_line_format(lines[0]) or
                                "proxy:on" not in lines[0]):
                            s3_bad_level_line.append(1)
                    time.sleep(0.0002)
            introspectors.append(threading.Thread(target=s3_introspect_worker))

        for th in callers:
            th.start()
        for th in introspectors:
            th.start()

        # 主线程：50 轮交替 SET v1/v2（写侧与 caller 的读侧并发交错）
        for round_ in range(k_set_rounds):
            resp = (YomkApi.set_console_log_proxy(s3_proxy_v2)
                    if round_ % 2 == 0
                    else YomkApi.set_console_log_proxy(s3_proxy_v1))
            if resp.status == ResStatus.eOk:
                s3_set_ok.append(1)
            time.sleep(0.0001)

        s3_stop.set()
        for th in callers:
            th.join()
        for th in introspectors:
            th.join()

    v1 = len(s3_v1_hits)
    v2 = len(s3_v2_hits)
    total = len(s3_log_total)

    check(len(s3_set_ok) == k_set_rounds,
          f"S3: SET 交替 {k_set_rounds} 轮全部 eOk")
    check(v1 + v2 == total,
          f"S3: 写读交错守恒 v1({v1}) + v2({v2}) == log 总数({total})——无 torn function（锁内拷贝快照生效）")
    check(v1 > 0, "S3: v1 实现被观测到")
    check(v2 > 0, "S3: v2 实现被观测到（更新确实生效）")
    check(total > 0, "S3: caller 线程确实执行了日志调用")
    check(len(s3_log_ok) == total, "S3: 全部 console_log 返回 eOk（穿透态不影响响应码）")
    check(len(s3_bad_level_line) == 0,
          f"S3: 并发内省 ALL 首行格式全部合法且含 proxy:on（非法 {len(s3_bad_level_line)} 次）")

    # ========================================================================
    # Section 4：并发 file logger 创建 × 写入 × 落盘（FileLogger 守恒）
    # ========================================================================
    # ---- S4a：同名争用——恰一胜，其余重名 eNo ----
    k_threads = 8
    same_dir = os.path.join(tmp_dir, "s4a")
    k_same_name = "lg3_s4_same"
    s4a_create_ok = []
    s4a_create_no = []
    console_before_a, file_before_a = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))

    gate = StartGate()
    workers = []
    for _t in range(k_threads):
        def s4a_worker():
            gate.wait()  # 屏障对齐：8 线程同时争抢独占锁
            resp = YomkApi.file_log_create(same_dir, k_same_name)
            if not is_legal_status(resp):
                bad_status.append(1)
            if resp.status == ResStatus.eOk:
                s4a_create_ok.append(1)
            elif (resp.status == ResStatus.eNo and
                  resp.msg == "logger name already exists."):
                s4a_create_no.append(1)
        workers.append(threading.Thread(target=s4a_worker))
    for th in workers:
        th.start()
    gate.release(k_threads)
    for th in workers:
        th.join()

    check(len(s4a_create_ok) == 1,
          f"S4a: 8 线程并发同名创建恰 1 个 eOk（实际 {len(s4a_create_ok)}）")
    check(len(s4a_create_no) == k_threads - 1,
          f"S4a: 其余 {k_threads - 1} 个 eNo \"logger name already exists.\""
          f"（实际 {len(s4a_create_no)}）——独占锁互斥下重名检查守恒")
    check(os.path.isfile(os.path.join(same_dir, k_same_name + ".log")),
          "S4a: 胜出者日志文件已落盘")

    console_after_a, file_after_a = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(file_after_a - file_before_a == 1,
          f"S4a: file logger 仅新增 1 个（无重复注册，实际新增 {file_after_a - file_before_a}）")
    check(console_after_a == console_before_a, "S4a: console logger 数量未受影响")

    # ---- S4b：异名并发——全部 eOk ----
    spread_dir = os.path.join(tmp_dir, "s4b")
    s4b_create_ok = []

    gate = StartGate()
    workers = []
    for t in range(k_threads):
        def s4b_worker(t=t):
            gate.wait()
            resp = YomkApi.file_log_create(spread_dir, "lg3_s4_t" + str(t))
            if not is_legal_status(resp):
                bad_status.append(1)
            if resp.status == ResStatus.eOk:
                s4b_create_ok.append(1)
        workers.append(threading.Thread(target=s4b_worker))
    for th in workers:
        th.start()
    gate.release(k_threads)
    for th in workers:
        th.join()

    check(len(s4b_create_ok) == k_threads,
          f"S4b: 8 线程异名并发创建全部 eOk（实际 {len(s4b_create_ok)}）")

    console_after_b, file_after_b = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(file_after_b - file_after_a == k_threads,
          f"S4b: file logger 新增恰 {k_threads} 个（实际新增 {file_after_b - file_after_a}）")

    # ---- S4c：并发写入 × 并发落盘守恒（2000 行不丢不重）----
    k_log_threads = 8
    k_logs_per_thread = 250
    k_sink_total = k_log_threads * k_logs_per_thread
    k_writer_threads = 2
    sink_dir = os.path.join(tmp_dir, "s4c")
    k_sink_name = "lg3_s4_sink"
    s4c_log_ok = []
    s4c_write_ok = []
    s4c_stop = threading.Event()

    check(YomkApi.file_log_create(sink_dir, k_sink_name).status == ResStatus.eOk,
          "S4c: 创建 sink file logger 成功")

    gate = StartGate()
    log_workers = []
    for t in range(k_log_threads):
        def log_worker(t=t):
            gate.wait()
            for i in range(k_logs_per_thread):
                resp = YomkApi.file_log_info_tag(
                    k_sink_name, "lg3s4c", "lg3s4c_t" + str(t) + "_i" + str(i))
                if not is_legal_status(resp):
                    bad_status.append(1)
                if resp.status == ResStatus.eOk:
                    s4c_log_ok.append(1)
        log_workers.append(threading.Thread(target=log_worker))

    # writer 线程：log 进行中持续 WRITE（缓冲交接竞态窗口）
    writers = []
    for _t in range(k_writer_threads):
        def writer_worker():
            while not s4c_stop.is_set():
                resp = YomkApi.file_log_write(k_sink_name)
                if not is_legal_status(resp):
                    bad_status.append(1)
                if resp.status == ResStatus.eOk:
                    s4c_write_ok.append(1)
                time.sleep(0.0002)
        writers.append(threading.Thread(target=writer_worker))

    for th in log_workers:
        th.start()
    for th in writers:
        th.start()
    gate.release(k_log_threads)
    for th in log_workers:
        th.join()
    s4c_stop.set()
    for th in writers:
        th.join()

    # 收尾：把残余缓冲全部落盘
    check(YomkApi.file_log_write(k_sink_name).status == ResStatus.eOk,
          "S4c: 收尾 WRITE 返回 eOk")

    check(len(s4c_log_ok) == k_sink_total,
          f"S4c: 并发写入响应守恒 eOk == {k_sink_total}（实际 {len(s4c_log_ok)}）")
    check(len(s4c_write_ok) > 0,
          f"S4c: writer 线程确实执行了落盘（{len(s4c_write_ok)} 次 eOk）")

    sink_content = read_file(os.path.join(sink_dir, k_sink_name + ".log"))
    sink_lines = count_lines(sink_content)
    check(sink_lines == k_sink_total,
          f"S4c: 落盘总行数 == {k_sink_total}（实际 {sink_lines}）"
          "——log/write 共用 m_logBufferMutex（P4-a 后缓冲为单次快照改写），缓冲交接不丢行不切行")

    sink_markers = collect_tail_markers(sink_content, "lg3s4c_t")
    check(len(sink_markers) == k_sink_total,
          f"S4c: 落盘唯一 marker 数 == {k_sink_total}（实际 {len(sink_markers)}）")
    check(count_bad_occurrence(sink_markers) == 0,
          "S4c: 全部 marker 恰落盘 1 次（不丢不重）——并发写落盘守恒核心证明")

    # ---- S4d：多写者并发 WRITE（缓冲清空后幂等，无重复落盘）----
    k_writer_threads_d = 4
    k_write_rounds_d = 10
    k_multi_lines = 100
    multi_dir = os.path.join(tmp_dir, "s4d")
    k_multi_name = "lg3_s4_multi"
    s4d_write_ok = []

    check(YomkApi.file_log_create(multi_dir, k_multi_name).status == ResStatus.eOk,
          "S4d: 创建 multi file logger 成功")
    for i in range(k_multi_lines):
        YomkApi.file_log_info_tag(k_multi_name, "lg3s4d", "lg3s4d_i" + str(i))

    gate = StartGate()
    workers = []
    for _t in range(k_writer_threads_d):
        def s4d_worker():
            gate.wait()
            for _r in range(k_write_rounds_d):
                resp = YomkApi.file_log_write(k_multi_name)
                if not is_legal_status(resp):
                    bad_status.append(1)
                if resp.status == ResStatus.eOk:
                    s4d_write_ok.append(1)
        workers.append(threading.Thread(target=s4d_worker))
    for th in workers:
        th.start()
    gate.release(k_writer_threads_d)
    for th in workers:
        th.join()

    check(len(s4d_write_ok) == k_writer_threads_d * k_write_rounds_d,
          f"S4d: {k_writer_threads_d} 线程 × {k_write_rounds_d} 轮并发 WRITE 全部 eOk"
          f"（实际 {len(s4d_write_ok)}）")

    multi_content = read_file(os.path.join(multi_dir, k_multi_name + ".log"))
    check(count_lines(multi_content) == k_multi_lines,
          f"S4d: 落盘行数 == {k_multi_lines}（实际 {count_lines(multi_content)}）"
          "——缓冲清空后 WRITE 幂等，无重复落盘")
    multi_markers = collect_tail_markers(multi_content, "lg3s4d_i")
    check(count_bad_occurrence(multi_markers) == 0,
          "S4d: 全部 marker 恰落盘 1 次（多写者无撕裂）")

    # ========================================================================
    # Section 5：全量 churn 混合 + P3-a 内省原子快照判别
    # ========================================================================
    k_churn_threads = 3
    # 1800 对（而非小规模）：判别式的灵敏度取决于"快照 console 段遍历耗时 W"与
    # "churn 一轮 console->file 间隔 G"的相对量级——违例需 W 完整覆盖 G。
    # 小规模（百级）下 W << G 几乎不可能触发（假阴性）；console 表提到 1800+ 项后
    # W 与 G 同量级，违例成为可稳定观测的事件（标定：120 对 -> 0（假阴性），
    # 900 对 -> 2，1800 对 -> LOGGERS 8 + ALL 7）
    k_churn_rounds = 600
    k_pair_total = k_churn_threads * k_churn_rounds  # 1800 对 logger
    k_introspect_threads = 2
    k_switch_threads = 2
    k_switch_rounds = 100
    k_console_threads = 2
    k_console_rounds = 200
    k_file_threads = 2
    k_file_rounds = 200

    pair_dir = os.path.join(tmp_dir, "s5")
    k_console_sink = "lg3_s5_sink"
    k_file_sink = "lg3_s5_sink_file"

    # 四级别调用分发（级别号 0=debug 1=info 2=warn 3=error）
    def console_level(level, tag, msg):
        if level == 0:
            return YomkApi.console_log_debug_tag(tag, msg)
        if level == 1:
            return YomkApi.console_log_info_tag(tag, msg)
        if level == 2:
            return YomkApi.console_log_warn_tag(tag, msg)
        return YomkApi.console_log_error_tag(tag, msg)

    def file_level(level, name, msg):
        if level == 0:
            return YomkApi.file_log_debug_tag(name, "lg3s5", msg)
        if level == 1:
            return YomkApi.file_log_info_tag(name, "lg3s5", msg)
        if level == 2:
            return YomkApi.file_log_warn_tag(name, "lg3s5", msg)
        return YomkApi.file_log_error_tag(name, "lg3s5", msg)

    def switch_level(level, turn_on):
        table_on = (YomkApi.on_console_log_debug, YomkApi.on_console_log_info,
                    YomkApi.on_console_log_warn, YomkApi.on_console_log_error)
        table_off = (YomkApi.off_console_log_debug, YomkApi.off_console_log_info,
                     YomkApi.off_console_log_warn, YomkApi.off_console_log_error)
        return (table_on if turn_on else table_off)[level]()

    # 预热 sink：churn 之外的所有日志调用一律使用已存在 logger，
    # 确保新 logger 只由 churn 线程"先 console 后 file"成对产生（判别式前提）
    r, _ = capture(lambda: console_level(1, k_console_sink, "lg3s5_warmup"))
    check(r.status == ResStatus.eOk, "S5: 预热 console sink logger 成功")
    check(YomkApi.file_log_create(pair_dir, k_file_sink).status == ResStatus.eOk,
          "S5: 预热 file sink logger 成功")

    # 基线快照
    base_c, base_f = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(base_c > 0 and base_f > 0,
          f"S5: 基线非空（console {base_c} / file {base_f}）")

    s5_pair_console_ok = []
    s5_pair_file_ok = []
    s5_pair_file_no = []
    s5_console_ok = []
    s5_file_ok = []
    s5_write_ok = []
    s5_switch_ok = []
    s5_introspect_ok = []
    s5_bad_level_line = []
    s5_stop = threading.Event()
    snaps_by_thread = [[] for _ in range(k_introspect_threads)]

    with contextlib.redirect_stdout(io.StringIO()):
        # S5 不断言 console 输出内容，捕获仅为降噪（避免上千行日志涌向终端）

        # churn 线程：每轮先 console 后 file 成对创建同名 logger
        churn_threads = []
        for t in range(k_churn_threads):
            def churn_worker(t=t):
                for round_ in range(k_churn_rounds):
                    name = "lg3_s5_pair_t" + str(t) + "_r" + str(round_)
                    console_resp = YomkApi.console_log_info_tag(
                        name, "lg3s5_pair")
                    if not is_legal_status(console_resp):
                        bad_status.append(1)
                    if console_resp.status == ResStatus.eOk:
                        s5_pair_console_ok.append(1)
                    # 成对语义：console 先于 file（任意真实时刻 C(T) >= F(T)）
                    file_resp = YomkApi.file_log_create(pair_dir, name)
                    if not is_legal_status(file_resp):
                        bad_status.append(1)
                    if file_resp.status == ResStatus.eOk:
                        s5_pair_file_ok.append(1)
                    elif file_resp.status == ResStatus.eNo:
                        s5_pair_file_no.append(1)
            churn_threads.append(threading.Thread(target=churn_worker))

        # 内省线程：紧密轮询 LOGGERS/ALL 直到 churn 完成（采样贯穿整个变更窗口）
        introspect_threads = []
        for t in range(k_introspect_threads):
            def s5_introspect_worker(t=t):
                local = []
                while not s5_stop.is_set():
                    snap_console, snap_file = count_logger_lines(
                        unpack_lines(YomkApi.logger_info_loggers()))

                    all_resp = YomkApi.logger_info_all()
                    if not is_legal_status(all_resp):
                        bad_status.append(1)
                    all_lines = unpack_lines(all_resp)
                    if all_lines:
                        if (not check_level_line_format(all_lines[0]) or
                                "proxy:on" not in all_lines[0]):
                            s5_bad_level_line.append(1)
                        snap_all_console, snap_all_file = count_logger_lines(
                            all_lines)
                        s5_introspect_ok.append(1)
                        # (consoleLines, fileLines, allLines, allConsole, allFile)
                        local.append((snap_console, snap_file, len(all_lines),
                                      snap_all_console, snap_all_file))
                    time.sleep(0.00001)
                snaps_by_thread[t] = local
            introspect_threads.append(
                threading.Thread(target=s5_introspect_worker))

        # 级别开关翻转线程
        switch_threads = []
        for t in range(k_switch_threads):
            def switch_worker(t=t):
                for round_ in range(k_switch_rounds):
                    level = (round_ + t) % 4
                    turn_on = ((round_ // 4) % 2) == 1
                    resp = switch_level(level, turn_on)
                    if not is_legal_status(resp):
                        bad_status.append(1)
                    if resp.status == ResStatus.eOk:
                        s5_switch_ok.append(1)
                    time.sleep(0.00005)
            switch_threads.append(threading.Thread(target=switch_worker))

        # console 日志线程（四级别轮转，已存在 sink tag）
        console_threads = []
        for t in range(k_console_threads):
            def console_worker(t=t):
                for round_ in range(k_console_rounds):
                    resp = console_level(
                        (round_ + t) % 4, k_console_sink,
                        "lg3s5_c" + str(t) + "_r" + str(round_))
                    if not is_legal_status(resp):
                        bad_status.append(1)
                    if resp.status == ResStatus.eOk:
                        s5_console_ok.append(1)
            console_threads.append(threading.Thread(target=console_worker))

        # file 日志线程（四级别轮转 + 周期性 WRITE）
        file_threads = []
        for t in range(k_file_threads):
            def file_worker(t=t):
                for round_ in range(k_file_rounds):
                    resp = file_level(
                        (round_ + t) % 4, k_file_sink,
                        "lg3s5_f" + str(t) + "_r" + str(round_))
                    if not is_legal_status(resp):
                        bad_status.append(1)
                    if resp.status == ResStatus.eOk:
                        s5_file_ok.append(1)
                    if round_ % 20 == 19:
                        write_resp = YomkApi.file_log_write(k_file_sink)
                        if not is_legal_status(write_resp):
                            bad_status.append(1)
                        if write_resp.status == ResStatus.eOk:
                            s5_write_ok.append(1)
            file_threads.append(threading.Thread(target=file_worker))

        for th in churn_threads:
            th.start()
        for th in introspect_threads:
            th.start()
        for th in switch_threads:
            th.start()
        for th in console_threads:
            th.start()
        for th in file_threads:
            th.start()

        # churn 先结束，再停内省线程（保证采样贯穿整个变更窗口）
        for th in churn_threads:
            th.join()
        s5_stop.set()
        for th in introspect_threads:
            th.join()
        for th in switch_threads:
            th.join()
        for th in console_threads:
            th.join()
        for th in file_threads:
            th.join()

    # ---- 收尾：四级别全 ON + file sink 落盘 ----
    check(YomkApi.on_console_log_debug().status ==
          ResStatus.eOk, "S5: 收尾 ON debug eOk")
    check(YomkApi.on_console_log_info().status ==
          ResStatus.eOk, "S5: 收尾 ON info eOk")
    check(YomkApi.on_console_log_warn().status ==
          ResStatus.eOk, "S5: 收尾 ON warn eOk")
    check(YomkApi.on_console_log_error().status ==
          ResStatus.eOk, "S5: 收尾 ON error eOk")
    check(YomkApi.file_log_write(k_file_sink).status == ResStatus.eOk,
          "S5: 收尾 file sink WRITE eOk")

    # ---- P3-a 判别式：对每个快照验证 C >= F（相对基线增量）----
    final_c, final_f = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    final_total = final_c + final_f

    loggers_violations = 0  # LOGGERS 端点 F > C 违例
    all_violations = 0      # ALL 端点 F > C 违例
    non_monotonic = 0       # 同线程快照序列总行数递减违例
    exceed_final = 0        # 快照行数超过最终值违例
    snapshot_count = 0
    for snaps in snaps_by_thread:
        prev_total = 0
        first = True
        for snap in snaps:
            snapshot_count += 1
            delta_c = snap[0] - base_c
            delta_f = snap[1] - base_f
            if delta_f > delta_c:
                loggers_violations += 1
            all_delta_c = snap[3] - base_c
            all_delta_f = snap[4] - base_f
            if all_delta_f > all_delta_c:
                all_violations += 1
            total = snap[0] + snap[1]
            if not first and total < prev_total:
                non_monotonic += 1
            first = False
            prev_total = total
            if total > final_total:
                exceed_final += 1

    check(snapshot_count > 0, f"S5: 收集到 {snapshot_count} 个内省快照")
    check(loggers_violations == 0,
          f"S5: LOGGERS 快照 C>=F 恒成立（违例 {loggers_violations} 个）"
          "——P3-a 嵌套双锁原子快照实证（分段锁下 file 段晚于 console 段会产生 F>C）")
    check(all_violations == 0,
          f"S5: ALL 快照 C>=F 恒成立（违例 {all_violations} 个）——list_all 同为原子快照")
    check(non_monotonic == 0,
          f"S5: 同线程快照序列总行数单调不减（递减 {non_monotonic} 次）——logger 只增不减且快照不丢行")
    check(exceed_final == 0,
          f"S5: 全部快照行数 <= 最终值 {final_total}（超出 {exceed_final} 次）——无幻觉行")
    check(len(s5_bad_level_line) == 0,
          f"S5: 并发下 ALL 首行格式全部合法且含 proxy:on（非法 {len(s5_bad_level_line)} 次）")
    check(len(bad_status) == 0,
          f"S5: 全部响应码属于 {{eInvalid,eOk,eNo}}（非法状态码 {len(bad_status)} 次）")

    # ---- churn 守恒 ----
    check(len(s5_pair_console_ok) == k_pair_total,
          f"S5: 成对创建 console 侧守恒 eOk == {k_pair_total}（实际 {len(s5_pair_console_ok)}）")
    check(len(s5_pair_file_ok) == k_pair_total,
          f"S5: 成对创建 file 侧守恒 eOk == {k_pair_total}（实际 {len(s5_pair_file_ok)}）")
    check(len(s5_pair_file_no) == 0, "S5: 成对创建无重名失败（唯一名设计）")
    check(final_c - base_c == k_pair_total,
          f"S5: console logger 最终新增 == {k_pair_total}（实际 {final_c - base_c}）——并发创建无丢失")
    check(final_f - base_f == k_pair_total,
          f"S5: file logger 最终新增 == {k_pair_total}（实际 {final_f - base_f}）——并发创建无丢失")

    check(len(s5_console_ok) == k_console_threads * k_console_rounds,
          f"S5: console 日志守恒 eOk == {k_console_threads * k_console_rounds}"
          f"（实际 {len(s5_console_ok)}）——级别开关并发翻转不影响响应码")
    check(len(s5_file_ok) == k_file_threads * k_file_rounds,
          f"S5: file 日志守恒 eOk == {k_file_threads * k_file_rounds}（实际 {len(s5_file_ok)}）")
    check(len(s5_switch_ok) == k_switch_threads * k_switch_rounds,
          f"S5: 级别开关翻转守恒 eOk == {k_switch_threads * k_switch_rounds}（实际 {len(s5_switch_ok)}）")
    check(len(s5_write_ok) > 0,
          f"S5: 周期性 WRITE 确实执行（{len(s5_write_ok)} 次 eOk）")
    check(len(s5_introspect_ok) > 0,
          f"S5: 内省线程确实观测到 ALL（{len(s5_introspect_ok)} 次）")

    # ---- 最终一致性：ALL 首行精确串 + 行数跨端点一致 + file sink 落盘守恒 ----
    final_all = unpack_lines(YomkApi.logger_info_all())
    check(len(final_all) > 0 and
          final_all[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "S5: 收尾四级别全 ON 后 ALL 首行为精确串（含 S3 已设的 proxy:on）")
    check(len(final_all) == final_total + 1,
          f"S5: ALL 行数 == LOGGERS 行数 + 1 首行（{len(final_all)} vs {final_total + 1}）——跨端点最终一致")

    sink_content = read_file(os.path.join(pair_dir, k_file_sink + ".log"))
    sink_lines = count_lines(sink_content)
    check(sink_lines == k_file_threads * k_file_rounds,
          f"S5: file sink 落盘行数 == {k_file_threads * k_file_rounds}（实际 {sink_lines}）"
          "——级别翻转与并发 WRITE 下不丢不重")

    # ============ 收尾清理 ============
    YomkApi.shutdown()
    shutil.rmtree(tmp_dir, ignore_errors=True)

    if g_failed == 0:
        print("TestYomkLoggerConcurrency all check passed.")
        return 0
    print(f"TestYomkLoggerConcurrency FAILED ({g_failed} checks failed).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
