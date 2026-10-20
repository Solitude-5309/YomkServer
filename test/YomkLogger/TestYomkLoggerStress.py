#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerStress.py —— YomkLogger 压力测试（LG4，6 节；
断言数随 N 变化，N=1000 时 61 条）

覆盖内容（6 节）：
S1 console 侧规模与热路径（Phase A N 个唯一 tag 惰性创建 + 1k/10k/100k/N 四规模点
ALL 延迟扫描 / A2 1000 行内容守恒抽样 + 时间戳精确校验 / B 命中写 / C 级别 OFF 短路 /
D proxy 短路 + 穿透恢复 / E 热 tag 重复写）；S2 file 侧激进规模（A N 个 logger 分片
1000/目录 / B N/10 单目录对照相 / C 磁盘足迹 + 内省条目双守恒）；S3 文件写入与大缓冲
落盘（A N 行分散 K sink 缓冲 / B 2N 行单 logger 大缓冲一次落盘 / C K sink 逐个 flush
行数守恒 / D 空缓冲重复 flush 文件不增长）；S4 内省端点 2N+ 条目响应（A 全规模延迟 /
B ALL 首行精确串 + 数据段与 LOGGERS 逐行一致 / C 单查 N 次 / D 内省快照与 create
相互阻塞量化）；S5 8 线程混合负载 churn + 全量删除回收（A 六调用 churn 守恒 /
B 分组全量回收 / C 残量扫除 -> LOGGERS 行数为 0 硬断言）；S6 收尾与磁盘清理（清理前
磁盘足迹守恒 / shutdown 计时 / rmtree / 临时目录不存在硬断言 / RSS 峰值）

语言机制说明：
- 规模经 YOMK_TEST_STRESS_SCALE 参数化（缺省 100000，clamp 下限 1000；run_tests.sh
  快速模式 export 10000、--full unset 回落缺省），激进规模口径：console tag=N；
  file logger=N（分片）+N/10（单目录对照）+2xN/100（S4-D）+1（S3 大缓冲）
  +N/500x8（S5 churn，建后即删但磁盘文件保留）→ 峰值约 1.11N+0.016N 个真实 .log 文件
  （FileLogger init 语义：创建即建空文件并建目录；删除接口不删磁盘文件）
- 断言总数随 N 变化（S1A 扫描点个数 = 去重后的 {1000,10000,100000,N} clamp 交集），
  N=1000 时 61 条、N=10000 时 62 条、N=100000 时 63 条
- ThreadSafeCoutCapture(discard) -> contextlib.redirect_stdout(io.StringIO()) 丢弃
  不取值（热路径相万级输出不占内存也不干扰计时）；取值相（A2/C probe/D 穿透）同款
  捕获后 getvalue
- std::atomic 计数 -> list.append + len()（GIL 单步原子）；停止门 -> threading.Event；
  起始屏障 -> StartGate（Condition 版 ready+go，30s 超时上限）
- 计时 steady_clock -> time.perf_counter；[BASELINE]/[ENV]/VmHWM 仅信息行不作断言
  （环境相关）；[ENV] 读 /proc/loadavg、/proc/meminfo MemAvailable，VmHWM 读
  /proc/self/status，读不到降级 n/a
- measureIntrospection 出参（badStatus/lastLines）-> 返回元组 (stat, bad, last_lines)；
  last_lines 取 len(resp.data) 免一次大规模列表拷贝（与解包后 size() 等价）
- S4B 逐行一致用切片相等 all_lines[1:] == loggers_lines
- S5B 全量回收的析构路径：python 服务层 delete_logger 锁外释放最后引用 -> __del__ ->
  write()（缓冲均已在 S3 flush 或为空 -> 空写 no-op；规模已在 S5 回收、
  析构无落盘 I/O 拖累）；S5A churn logger 每轮 1 行由当轮删除时 __del__ 落盘，
  磁盘文件保留（删除接口不删磁盘文件）
- proxy 安装后全程不卸载（P4-e 卸载语义由 Lifecycle S3 覆盖），S1D 末恢复穿透态，
  不影响后续 Section 的响应码与落盘断言
- 唯一 tag 直调 YomkApi.console_log_info_tag 运行时拼名，循环内拼名构造 N 个唯一 tag

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
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


def stress_scale():
    # 环境变量 YOMK_TEST_STRESS_SCALE（run_tests.sh 快速模式降规模用），
    # 缺省 10 万，clamp 下限 1000
    env = os.environ.get("YOMK_TEST_STRESS_SCALE")
    if not env:
        return 100000
    try:
        v = int(env)
    except ValueError:
        return 100000
    return max(v, 1000)


def make_name(prefix, index):
    # 带零填充的唯一名（setw(8) setfill('0') 对齐）
    return prefix + str(index).zfill(8)


def unpack_lines(resp):
    # 解包 LOGGERS/ALL 的 list[str]；失败返回空列表
    if resp.status != ResStatus.eOk or not resp.data:
        return []
    return list(resp.data)


def count_logger_lines(lines):
    # 统计 LOGGERS/ALL 数据行中的 console/file 段行数
    console_lines = 0
    file_lines = 0
    for line in lines:
        if line.endswith(" [console]"):
            console_lines += 1
            continue
        if " [file] dir:" in line:
            file_lines += 1
    return console_lines, file_lines


def parse_logger_name(line):
    # 从内省数据行解析 logger 名（"name [console]" / "name [file] dir:..."）
    pos = line.find(" [file] dir:")
    if pos != -1:
        return line[:pos]
    suffix = " [console]"
    if len(line) > len(suffix) and line.endswith(suffix):
        return line[:-len(suffix)]
    return line


def is_legal_status(resp):
    return resp.status in (ResStatus.eInvalid, ResStatus.eOk, ResStatus.eNo)


def check_time_format(line):
    # 时间戳格式精确校验：行首 25 字符须为 "[dddd-dd-dd dd:dd:dd.ddd]"
    return TIME_RE.match(line) is not None


def collect_tail_markers(text, prefix):
    # 统计"行尾最后一段"以 prefix 开头的 marker 出现次数（内容守恒判定）
    markers = {}
    for line in text.splitlines():
        if not line:
            continue
        tail = line.rsplit(" ", 1)[-1]
        if tail.startswith(prefix):
            markers[tail] = markers.get(tail, 0) + 1
    return markers


def count_bad_occurrence(markers):
    return sum(1 for c in markers.values() if c != 1)


def count_file_lines(path):
    # 统计文件非空行数；打开失败返回 (False, 0)
    try:
        with open(path) as f:
            return True, sum(1 for ln in f if ln.strip())
    except OSError:
        return False, 0


def count_disk_files(root):
    # 递归统计目录下普通文件数（S6 清理前的磁盘足迹守恒用）
    if not os.path.isdir(root):
        return 0
    total = 0
    for _dirpath, _dirnames, filenames in os.walk(root):
        total += len(filenames)
    return total


def introspection_rounds(entries):
    # 内省轮次策略：条目越多轮次越少（控制单次运行时长，数据点仍具统计意义）
    if entries <= 2000:
        return 200
    if entries <= 20000:
        return 100
    if entries <= 200000:
        return 20
    return 5


def measure_introspection(rounds, use_all):
    # 对内省端点做 rounds 次调用，返回延迟分布；非 eOk 计入 bad
    count = 0
    total_ms = 0.0
    min_ms = 0.0
    max_ms = 0.0
    bad = 0
    last_lines = 0
    for _ in range(rounds):
        t0 = time.perf_counter()
        resp = YomkApi.logger_info_all() if use_all else YomkApi.logger_info_loggers()
        ms = (time.perf_counter() - t0) * 1000.0
        if resp.status != ResStatus.eOk:
            bad += 1
            continue
        last_lines = len(resp.data) if resp.data else 0
        if count == 0:
            min_ms = max_ms = ms
        else:
            if ms < min_ms:
                min_ms = ms
            if ms > max_ms:
                max_ms = ms
        total_ms += ms
        count += 1
    avg_ms = total_ms / count if count else 0.0
    return (count, min_ms, avg_ms, max_ms), bad, last_lines


def record_baseline(section, ops, elapsed_ms):
    # 基线记录（仅打印，不断言）：耗时(ms) 与吞吐(ops/s)
    ops_per_sec = ops * 1000.0 / elapsed_ms if elapsed_ms > 0 else 0.0
    print(
        f"[BASELINE] {section}: {ops_per_sec:.0f} ops/s ({ops} ops in {elapsed_ms:.1f} ms)")


def record_latency(section, stat):
    count, min_ms, avg_ms, max_ms = stat
    print(f"[BASELINE] {section}: avg {avg_ms:.3f} ms "
          f"(min {min_ms:.3f} / max {max_ms:.3f} over {count} calls)")


def read_vm_hwm():
    # 进程 RSS 峰值（Linux: /proc/self/status VmHWM；其他平台 n/a）——信息行非断言
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    return " " + line[len("VmHWM:"):].strip()
    except OSError:
        pass
    return " n/a"


def read_env_snapshot():
    # 采样环境快照：CPU 核数 + 负载 + 可用内存，
    # 使 [BASELINE] 数据自带环境标注；纯信息行非断言，读不到降级 n/a
    env = "cpu=" + str(os.cpu_count() or 0)
    load = "n/a"
    try:
        with open("/proc/loadavg") as f:
            parts = f.readline().split()
            if len(parts) >= 3:
                load = "/".join(parts[:3])
    except OSError:
        pass
    env += " loadavg=" + load
    mem_avail = "n/a"
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    value = line[len("MemAvailable:"):].strip()
                    if value:
                        mem_avail = value
                    break
    except OSError:
        pass
    env += " memAvail=" + mem_avail
    return env


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

    def release(self, parties, timeout_s=30.0):
        deadline = time.monotonic() + timeout_s
        with self._cv:
            while self._ready < parties and time.monotonic() < deadline:
                self._cv.wait(timeout=0.01)
            self._go = True
            self._cv.notify_all()


def main():
    print("=== TestYomkLoggerStress (LG4) ===")

    n = stress_scale()
    print(f"[CONFIG] YOMK_TEST_STRESS_SCALE = {n}")
    # 环境标注先于一切 [BASELINE] 输出，使本次所有性能数据可按环境归因
    print("[ENV] " + read_env_snapshot())

    server = YomkApi.init(1)
    check(server is not None, "YomkApi.init 返回非空服务器")

    tmp_dir = os.path.join(tempfile.gettempdir(),
                           "yomk_logger_lg4_" + str(os.getpid()))
    os.makedirs(tmp_dir, exist_ok=True)
    check(os.path.isdir(tmp_dir), f"临时目录创建成功: {tmp_dir}")

    # ---- 规模派生量（各 Section 共享同一口径：S5 全量回收与 S6 磁盘守恒都按此推算）----
    k_shard_size = 1000  # S2 分片粒度（每目录 1000 个 .log）
    shard_count = (n + k_shard_size - 1) // k_shard_size
    m = n // 10                                    # S2 单目录对照相规模
    # S3 缓冲相 sink logger 数（复用 S2 产物）
    k = min(n, 100)
    d = n // 100 if n // 100 > 0 else 1            # S4-D 并发 create 规模
    churn_rounds = max(n // 500, 2)                # S5 每线程 churn 轮数
    introspect_every = max(churn_rounds // 10, 1)
    introspect_per_thread = (
        churn_rounds + introspect_every - 1) // introspect_every
    k_threads = 8
    churn_total = churn_rounds * k_threads
    s2_dir = os.path.join(tmp_dir, "s2")
    s2_single = os.path.join(tmp_dir, "s2single")
    print(f"[CONFIG] N={n} shard={shard_count} M={m} K={k} D={d} "
          f"churn={churn_total}({k_threads}x{churn_rounds}, introspect every {introspect_every})")

    # 分片目录路径预构造（仅 shard_count 条），排除循环内字符串拼接开销
    shard_dirs = [os.path.join(s2_dir, "shard_" + str(s))
                  for s in range(shard_count)]

    # 起点基线：库内部不使用 LOG 家族宏 → console 表恰为构造函数预置的 MainLogger
    base_c, base_f = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(base_c == 1 and base_f == 0,
          f"起点内省基线 console=1(MainLogger)/file=0（实测 {base_c}/{base_f}）")

    # ========================================================================
    # S1：console 侧规模与热路径
    # ========================================================================
    # 名字表预构造：排除拼名开销，使各相基线可比
    tags = [make_name("lg4s1_tag_", i) for i in range(n)]
    hot_tag = tags[0] if tags else "lg4s1_hot"

    # 规模-延迟扫描点（P4-c）：console 表从 1 条线性增长到 N+1 条，是全程唯一能覆盖
    # 1k/10k/100k 中小规模的窗口——S2 起 file 表叠加，条目数只增不减，无法再降规模
    scan_points = []
    for raw in (1000, 10000, 100000, n):
        cp = min(raw, n)
        if not scan_points or scan_points[-1] != cp:
            scan_points.append(cp)

    # ---- Phase A：N 个唯一 tag 首次写入（惰性创建 N 个 ConsoleLogger）----
    # 分段计时：内省扫描落在创建计时区间之外，不污染 S1A 吞吐基线
    ok_a = 0
    elapsed_a = 0.0
    created_a = 0
    for cp in scan_points:
        with contextlib.redirect_stdout(io.StringIO()):
            t0 = time.perf_counter()
            for i in range(created_a, cp):
                if YomkApi.console_log_info_tag(tags[i], "lg4s1a_marker").status == ResStatus.eOk:
                    ok_a += 1
            elapsed_a += (time.perf_counter() - t0) * 1000.0
        created_a = cp

        # 扫描数据点：条目 = created_a 个 tag + MainLogger（此刻 file 侧仍为 0）；
        # ALL 首行为级别状态行，故总行数 = 条目数 + 1
        entries = created_a + 1
        stat_all, bad_all, lines_all = measure_introspection(
            introspection_rounds(entries), True)
        print(f"[BASELINE] S1A_scan_ALL@entries={entries}: avg {stat_all[2]:.3f} ms "
              f"(min {stat_all[1]:.3f} / max {stat_all[3]:.3f} over {stat_all[0]} calls, "
              f"lines {lines_all})")
        check(bad_all == 0 and lines_all == entries + 1,
              f"S1A: 扫描点 entries={entries} 内省 eOk + 行数守恒")

    record_baseline("S1A_console_create_unique_tag", n, elapsed_a)
    check(ok_a == n, f"S1A: 唯一 tag 写入 eOk 守恒 {ok_a}/{n}")

    console_entries, _file_entries = count_logger_lines(
        unpack_lines(YomkApi.logger_info_loggers()))
    check(console_entries == n + 1,
          f"S1A: console 表条目 = N + MainLogger（实测 {console_entries}）")

    # ---- Phase A2：内容守恒抽样（1000 行累积捕获）+ 时间戳格式精确校验 ----
    sample = min(1000, n)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        for i in range(sample):
            YomkApi.console_log_info_tag(tags[i], "lg4s1a2_" + str(i))
        out_a2 = buf.getvalue()
    markers_a2 = collect_tail_markers(out_a2, "lg4s1a2_")
    check(len(markers_a2) == sample,
          f"S1A2: 抽样 marker 全数命中（不丢）{len(markers_a2)}/{sample}")
    check(count_bad_occurrence(markers_a2) == 0, "S1A2: 抽样 marker 无重复（不重）")

    sampled_lines = 0
    bad_format = 0
    for line in out_a2.splitlines():
        if "lg4s1a2_" not in line:
            continue  # 框架自身噪声行不参与判定
        sampled_lines += 1
        if not check_time_format(line):
            bad_format += 1
    check(sampled_lines == sample and bad_format == 0,
          f"S1A2: 抽样行时间戳格式全部合法（P4-b 改写等价性，非法 {bad_format} 条）")

    # ---- Phase B：命中路径写入（N 个既有 tag 各 1 次，含时间戳构造 + stdout）----
    ok_b = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            if YomkApi.console_log_info_tag(tags[i], "lg4s1b_marker").status == ResStatus.eOk:
                ok_b += 1
        elapsed_b = (time.perf_counter() - t0) * 1000.0
    record_baseline("S1B_console_write_hit", n, elapsed_b)
    check(ok_b == n, f"S1B: 命中写 eOk 守恒 {ok_b}/{n}")

    # ---- Phase C：级别 OFF 短路（开关关断后仍 eOk，但不产生输出）----
    check(YomkApi.off_console_log_info().status ==
          ResStatus.eOk, "S1C: OFF info 返回 eOk")
    probe = min(100, n)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        for i in range(probe):
            YomkApi.console_log_info_tag(tags[i], "lg4s1c_off_marker")
        out_off = buf.getvalue()
    check("lg4s1c_off_marker" not in out_off,
          f"S1C: 级别 OFF 期间无输出（抽样 {probe} 行）")

    ok_c = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            if YomkApi.console_log_info_tag(tags[i], "lg4s1c_marker").status == ResStatus.eOk:
                ok_c += 1
        elapsed_c = (time.perf_counter() - t0) * 1000.0
    record_baseline("S1C_console_level_off_shortcut", n, elapsed_c)
    check(ok_c == n, f"S1C: 级别 OFF 短路仍 eOk 守恒 {ok_c}/{n}")
    check(YomkApi.on_console_log_info().status ==
          ResStatus.eOk, "S1C: 恢复 ON info 返回 eOk")

    # ---- Phase D：proxy 短路（置于 console 末相：本测试安装后不卸载，靠恒穿透
    # 避免影响后续 Section；P4-e 的卸载语义由 Lifecycle S3 覆盖）----
    proxy_hits = []
    proxy_state = {"through": False}

    def lg4_proxy(_log_pkg):
        proxy_hits.append(1)
        return proxy_state["through"]

    check(YomkApi.set_console_log_proxy(lg4_proxy).status == ResStatus.eOk,
          "S1D: 安装计数 proxy 返回 eOk")
    del proxy_hits[:]
    ok_d = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            if YomkApi.console_log_info_tag(tags[i], "lg4s1d_marker").status == ResStatus.eOk:
                ok_d += 1
        elapsed_d = (time.perf_counter() - t0) * 1000.0
    record_baseline("S1D_console_proxy_shortcut", n, elapsed_d)
    check(ok_d == n, f"S1D: proxy 短路仍 eOk 守恒 {ok_d}/{n}")
    check(len(proxy_hits) == n, f"S1D: proxy 命中守恒 {len(proxy_hits)}/{n}")
    # 恢复穿透态：此后 console 日志仍会真实输出（后续 Section 不校验其内容）
    proxy_state["through"] = True
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        YomkApi.console_log_info_tag(hot_tag, "lg4s1d_through_marker")
        out_through = buf.getvalue()
    check("lg4s1d_through_marker" in out_through,
          "S1D: proxy 穿透态恢复默认输出（P4-e 登记依据）")

    # ---- Phase E：热 tag 重复写（单 logger，ConsoleLogger m_mutex 恒定同一把）----
    ok_e = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            if YomkApi.console_log_info_tag(hot_tag, "lg4s1e_marker").status == ResStatus.eOk:
                ok_e += 1
        elapsed_e = (time.perf_counter() - t0) * 1000.0
    record_baseline("S1E_console_hot_tag", n, elapsed_e)
    check(ok_e == n, f"S1E: 热 tag 写 eOk 守恒 {ok_e}/{n}")

    print("[BASELINE] S1_VmHWM:" + read_vm_hwm())

    # ========================================================================
    # S2：file 侧激进规模（真实建目录 + 建 .log 文件，全程持 file 表独占锁）
    # ========================================================================
    os.makedirs(s2_dir, exist_ok=True)
    os.makedirs(s2_single, exist_ok=True)

    # ---- Phase A：N 个 file logger 分片创建（目录由 FileLogger.init 自行创建）----
    ok_a = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            if YomkApi.file_log_create(shard_dirs[i // k_shard_size],
                                       make_name("lg4s2_fl_", i)).status == ResStatus.eOk:
                ok_a += 1
        elapsed_a = (time.perf_counter() - t0) * 1000.0
    record_baseline("S2A_file_create_sharded", n, elapsed_a)
    check(ok_a == n, f"S2A: 分片创建 eOk 守恒 {ok_a}/{n}")

    # ---- Phase B：单目录对照相（M 个 logger 全在同一目录）----
    # 与 Phase A 的每条均摊耗时对比，暴露单目录 inode 膨胀 / makedirs 退化
    ok_b = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(m):
            if YomkApi.file_log_create(s2_single,
                                       make_name("lg4s2_single_", i)).status == ResStatus.eOk:
                ok_b += 1
        elapsed_b = (time.perf_counter() - t0) * 1000.0
    record_baseline("S2B_file_create_singledir", m, elapsed_b)
    check(ok_b == m, f"S2B: 单目录创建 eOk 守恒 {ok_b}/{m}")
    print(f"[BASELINE] S2_per_create_us: sharded {elapsed_a * 1000.0 / n:.1f} us vs singledir "
          f"{elapsed_b * 1000.0 / m:.1f} us ({shard_count} shards vs 1 dir)")

    # ---- Phase C：磁盘足迹与内省条目双守恒 ----
    disk_files = count_disk_files(s2_dir) + count_disk_files(s2_single)
    check(disk_files == n + m,
          f"S2C: 磁盘 .log 文件数守恒 = N + N/10（实测 {disk_files}/{n + m}）")

    c, f = count_logger_lines(unpack_lines(YomkApi.logger_info_loggers()))
    check(c == n + 1 and f == n + m,
          f"S2C: 内省条目守恒 console=N+1 file=N+N/10（实测 {c}/{f}）")
    file_total = f

    print("[BASELINE] S2_VmHWM:" + read_vm_hwm())

    # ========================================================================
    # S3：文件路径写入与大缓冲落盘（P4-a 验证点：write() 单次快照改写）
    # ========================================================================
    # sink 复用 S2 Phase A 的前 K 个 file logger（均落在 shard_0）
    sink_dir = shard_dirs[0] if shard_dirs else s2_dir
    sinks = [make_name("lg4s2_fl_", i) for i in range(k)]

    # ---- Phase A：N 行分散写入 K 个 logger 的内存缓冲（不落盘）----
    lines_per_sink = n // k
    total_lines = lines_per_sink * k
    ok_a = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for r_ in range(lines_per_sink):
            for sink in sinks:
                if YomkApi.file_log_info_tag(sink, "lg4s3a",
                                             "line_" + str(r_)).status == ResStatus.eOk:
                    ok_a += 1
        elapsed_a = (time.perf_counter() - t0) * 1000.0
    record_baseline("S3A_file_buffer_write", total_lines, elapsed_a)
    check(ok_a == total_lines, f"S3A: 缓冲写 eOk 守恒 {ok_a}/{total_lines}")
    open_ok, before_lines = count_file_lines(
        os.path.join(sink_dir, sinks[0] + ".log"))
    check(open_ok and before_lines == 0, "S3A: 未 flush 前 sink0 磁盘文件仍 0 行（缓冲语义）")

    # ---- Phase B：单 logger 大缓冲一次落盘（P4-a 主验证点）----
    s3_dir = os.path.join(tmp_dir, "s3")
    os.makedirs(s3_dir, exist_ok=True)
    big_name = "lg4s3_big"
    check(YomkApi.file_log_create(s3_dir, big_name).status == ResStatus.eOk,
          "S3B: 大缓冲 logger 创建 eOk")
    big_path = os.path.join(s3_dir, big_name + ".log")
    big_lines = 2 * n
    ok_b = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(big_lines):
            if YomkApi.file_log_info_tag(big_name, "lg4s3b",
                                         "line_" + str(i)).status == ResStatus.eOk:
                ok_b += 1
        elapsed_b = (time.perf_counter() - t0) * 1000.0
    record_baseline("S3B_file_buffer_fill_big", big_lines, elapsed_b)
    check(ok_b == big_lines, f"S3B: 大缓冲填充 eOk 守恒 {ok_b}/{big_lines}")

    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        flush_big = YomkApi.file_log_write(big_name)
        elapsed_flush = (time.perf_counter() - t0) * 1000.0
    check(flush_big.status == ResStatus.eOk, "S3B: 大缓冲一次落盘 eOk")
    big_bytes = os.path.getsize(big_path)
    big_mb = big_bytes / (1024.0 * 1024.0)
    big_mb_per_sec = big_mb / \
        (elapsed_flush / 1000.0) if elapsed_flush > 0 else 0.0
    print(f"[BASELINE] S3B_flush_big: {elapsed_flush:.1f} ms / {big_mb:.2f} MB / "
          f"{big_mb_per_sec:.2f} MB/s ({big_lines} lines)")
    open_ok, read_back = count_file_lines(big_path)
    check(open_ok and read_back == big_lines,
          f"S3B: 大缓冲读回行数守恒 {read_back}/{big_lines}")

    # ---- Phase C：K 个 logger 逐个 flush，逐文件行数守恒 ----
    ok_c = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for sink in sinks:
            if YomkApi.file_log_write(sink).status == ResStatus.eOk:
                ok_c += 1
        elapsed_c = (time.perf_counter() - t0) * 1000.0
    record_baseline("S3C_file_flush_k_loggers", k, elapsed_c)
    check(ok_c == k, f"S3C: K 个 logger flush eOk 守恒 {ok_c}/{k}")
    sum_lines = 0
    bad_files = 0
    for sink in sinks:
        open_ok, sink_lines = count_file_lines(
            os.path.join(sink_dir, sink + ".log"))
        if not open_ok or sink_lines != lines_per_sink:
            bad_files += 1
        sum_lines += sink_lines
    check(bad_files == 0, f"S3C: 逐 sink 文件行数 = N/K（异常文件 {bad_files} 个）")
    check(sum_lines == total_lines,
          f"S3C: K 个文件行数总和守恒 {sum_lines}/{total_lines}")

    # ---- Phase D：空缓冲重复 flush（既有语义：文件不增长，writeFileLog 仍 eOk）----
    size_before = os.path.getsize(big_path)
    ok_d = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for _ in range(n):
            if YomkApi.file_log_write(big_name).status == ResStatus.eOk:
                ok_d += 1
        elapsed_d = (time.perf_counter() - t0) * 1000.0
    record_baseline("S3D_file_flush_empty_buffer", n, elapsed_d)
    check(ok_d == n, f"S3D: 空缓冲重复 flush 仍 eOk 守恒 {ok_d}/{n}")
    size_after = os.path.getsize(big_path)
    check(size_after == size_before,
          f"S3D: 空缓冲 flush 文件不增长（{size_after} == {size_before}）")

    print("[BASELINE] S3_VmHWM:" + read_vm_hwm())

    # ========================================================================
    # S4：内省端点在 2N+ 条目下的响应（P4-c 验证点：双锁快照窗口）
    # ========================================================================
    loggers_lines = unpack_lines(YomkApi.logger_info_loggers())
    c, f = count_logger_lines(loggers_lines)
    entries = c + f
    print(f"[CONFIG] S4 entries = {entries} (console {c} / file {f}) "
          f"rounds = {introspection_rounds(entries)}")
    check(c == n + 1 and f == file_total + 1,
          f"S4: 全规模条目守恒 console=N+1 file=N+N/10+1（实测 {c}/{f}）")

    # ---- Phase A：全规模延迟（LOGGERS / ALL 各一轮扫描）----
    rounds = introspection_rounds(entries)
    stat_l, bad_l, lines_l = measure_introspection(rounds, False)
    record_latency("S4A_loggers", stat_l)
    check(bad_l == 0 and lines_l == entries,
          f"S4A: LOGGERS eOk + 行数守恒（{lines_l}/{entries}）")
    stat_a, bad_a, lines_a = measure_introspection(rounds, True)
    record_latency("S4A_all", stat_a)
    check(bad_a == 0 and lines_a == entries + 1,
          f"S4A: ALL eOk + 行数守恒（含状态行，{lines_a}/{entries + 1}）")

    # ---- Phase B：ALL 首行精确串 + 数据段与 LOGGERS 逐行一致（P3-a 原子快照可观测等价）----
    all_lines = unpack_lines(YomkApi.logger_info_all())
    check(len(all_lines) > 0 and
          all_lines[0] == "console:debug:on info:on warn:on error:on proxy:on",
          "S4B: ALL 首行状态串精确匹配（proxy 已在 S1D 安装）")
    identical = (len(all_lines) == entries +
                 1 and len(loggers_lines) == entries)
    if identical:
        identical = (all_lines[1:] == loggers_lines)
    check(identical, "S4B: ALL 数据段与 LOGGERS 逐行一致（嵌套双锁原子快照）")

    # ---- Phase C：单查 N 次（偶数查 console 名、奇数查 file 名；两表查找热路径量化点）----
    # 名字域校验：偶数 i 的 lg4s1_tag_i 由 S1A 创建、奇数 i 的 lg4s2_fl_i 由 S2A 创建，
    # 全部命中，CHECK(okProbe == probes) 的守恒语义成立
    probes = n
    ok_probe = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(probes):
            name = make_name("lg4s1_tag_", i) if i % 2 == 0 else make_name(
                "lg4s2_fl_", i)
            if YomkApi.logger_info_logger(name).status == ResStatus.eOk:
                ok_probe += 1
        elapsed_probe = (time.perf_counter() - t0) * 1000.0
    record_baseline("S4C_logger_single_query", probes, elapsed_probe)
    check(ok_probe == probes, f"S4C: 单查 eOk 守恒 {ok_probe}/{probes}")

    # ---- Phase D：内省快照与 createFileLogger 独占锁相互阻塞量化 ----
    s4_solo = os.path.join(tmp_dir, "s4dsolo")
    s4_conc = os.path.join(tmp_dir, "s4d")
    os.makedirs(s4_solo, exist_ok=True)
    os.makedirs(s4_conc, exist_ok=True)

    # 对照相：无内省并发时 D 次 create 的均摊延迟
    solo_ok = 0
    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(d):
            if YomkApi.file_log_create(s4_solo,
                                       make_name("lg4s4d_solo_", i)).status == ResStatus.eOk:
                solo_ok += 1
        solo_ms = (time.perf_counter() - t0) * 1000.0
    check(solo_ok == d, f"S4D: 对照相 create eOk 守恒 {solo_ok}/{d}")

    s4d_stop = threading.Event()
    s4d_create_ok = []
    s4d_create_ns = []
    s4d_snap_count = []
    s4d_snap_non_mono = []
    s4d_snap_bad_status = []

    def snap_worker():
        prev_lines = 0
        while not s4d_stop.is_set():
            resp = YomkApi.logger_info_all()
            if resp.status != ResStatus.eOk:
                s4d_snap_bad_status.append(1)
                continue
            lines = len(resp.data) if resp.data else 0
            s4d_snap_count.append(1)
            if lines < prev_lines:
                s4d_snap_non_mono.append(1)
            prev_lines = lines
            time.sleep(0.0001)

    snap_thread = threading.Thread(target=snap_worker)
    snap_thread.start()
    # 等首个快照落地再开始创建，保证两个窗口真实重叠（否则 create 可能先跑完）
    wait_s = 0.0
    while len(s4d_snap_count) == 0 and wait_s < 5.0:
        time.sleep(0.001)
        wait_s += 0.001

    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(d):
            c0 = time.perf_counter()
            if YomkApi.file_log_create(s4_conc,
                                       make_name("lg4s4d_fl_", i)).status == ResStatus.eOk:
                s4d_create_ok.append(1)
            s4d_create_ns.append((time.perf_counter() - c0) * 1e9)
        conc_ms = (time.perf_counter() - t0) * 1000.0
    s4d_stop.set()
    snap_thread.join()

    solo_per_us = solo_ms * 1000.0 / d
    conc_per_us = (sum(s4d_create_ns) / 1000.0) / d
    ratio = conc_per_us / solo_per_us if solo_per_us > 0 else 0.0
    print(f"[BASELINE] S4D_create_latency_us: solo {solo_per_us:.1f} vs "
          f"under_introspection {conc_per_us:.1f} (ratio {ratio:.2f}, "
          f"snapshots {len(s4d_snap_count)}, wall {conc_ms:.1f} ms)")
    check(len(s4d_create_ok) == d,
          f"S4D: 并发相 create eOk 守恒 {len(s4d_create_ok)}/{d}")
    check(len(s4d_snap_bad_status) == 0,
          f"S4D: 内省快照无非 eOk（异常 {len(s4d_snap_bad_status)} 次）")
    check(len(s4d_snap_count) > 0,
          f"S4D: 内省线程确实产生快照（{len(s4d_snap_count)} 次）")
    check(len(s4d_snap_non_mono) == 0,
          f"S4D: 快照条目数单调不减（P3-a 语义在规模下仍成立，违例 {len(s4d_snap_non_mono)} 次）")

    print("[BASELINE] S4_VmHWM:" + read_vm_hwm())

    # ========================================================================
    # S5：8 线程混合负载 churn + 全量删除回收
    # ========================================================================
    s5_dir = os.path.join(tmp_dir, "s5")
    os.makedirs(s5_dir, exist_ok=True)

    # ---- Phase A：8 线程 churn（console 写 + file 建/写 + 内省 + 删除 + 删除后单查）----
    s5_console_ok = []
    s5_file_ok = []
    s5_log_ok = []
    s5_introspect_ok = []
    s5_delete_ok = []
    s5_post_delete_no = []
    s5_bad_status = []

    gate = StartGate()
    workers = []
    for t in range(k_threads):
        def churn_worker(t=t):
            gate.wait()
            for r_ in range(churn_rounds):
                name = "lg4s5_t" + str(t) + "_r" + str(r_)

                rc = YomkApi.console_log_info_tag(name, "lg4s5_console")
                if rc.status == ResStatus.eOk:
                    s5_console_ok.append(1)
                elif not is_legal_status(rc):
                    s5_bad_status.append(1)

                rf = YomkApi.file_log_create(s5_dir, name)
                if rf.status == ResStatus.eOk:
                    s5_file_ok.append(1)
                elif not is_legal_status(rf):
                    s5_bad_status.append(1)

                rl = YomkApi.file_log_info_tag(name, "lg4s5", "file_line")
                if rl.status == ResStatus.eOk:
                    s5_log_ok.append(1)
                elif not is_legal_status(rl):
                    s5_bad_status.append(1)

                if (r_ % introspect_every) == 0:
                    ri = YomkApi.logger_info_all()
                    if ri.status == ResStatus.eOk:
                        s5_introspect_ok.append(1)
                    elif not is_legal_status(ri):
                        s5_bad_status.append(1)

                # 同名同时命中两表：单端点一次删除 console + file
                rd = YomkApi.file_log_delete(name)
                if rd.status == ResStatus.eOk:
                    s5_delete_ok.append(1)
                elif not is_legal_status(rd):
                    s5_bad_status.append(1)

                rn = YomkApi.logger_info_logger(name)
                if rn.status == ResStatus.eNo:
                    s5_post_delete_no.append(1)
                elif not is_legal_status(rn):
                    s5_bad_status.append(1)
        workers.append(threading.Thread(target=churn_worker))

    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for th in workers:
            th.start()
        gate.release(k_threads)
        for th in workers:
            th.join()
        elapsed_churn = (time.perf_counter() - t0) * 1000.0

    introspect_total = k_threads * introspect_per_thread
    record_baseline("S5A_mixed_churn", churn_total *
                    5 + introspect_total, elapsed_churn)
    check(len(s5_console_ok) == churn_total,
          f"S5A: console 写 eOk 守恒 {len(s5_console_ok)}/{churn_total}")
    check(len(s5_file_ok) == churn_total,
          f"S5A: file 建 eOk 守恒 {len(s5_file_ok)}/{churn_total}")
    check(len(s5_log_ok) == churn_total,
          f"S5A: file 写 eOk 守恒 {len(s5_log_ok)}/{churn_total}")
    check(len(s5_introspect_ok) == introspect_total,
          f"S5A: 内省 eOk 守恒 {len(s5_introspect_ok)}/{introspect_total}")
    check(len(s5_delete_ok) == churn_total,
          f"S5A: 删除 eOk 守恒 {len(s5_delete_ok)}/{churn_total}")
    check(len(s5_post_delete_no) == churn_total,
          f"S5A: 删除后单查 eNo 守恒 {len(s5_post_delete_no)}/{churn_total}")
    check(len(s5_bad_status) == 0,
          f"S5A: 无非法状态码（异常 {len(s5_bad_status)} 次）")
    c, f = count_logger_lines(unpack_lines(YomkApi.logger_info_loggers()))
    check(c == n + 1 and f == file_total + 1 + 2 * d,
          f"S5A: churn 后条目净变化 0（实测 console {c} / file {f}）")

    # ---- Phase B：全量回收 S1/S2/S3/S4 遗留的所有 logger ----
    del_console_ok = 0
    del_file_ok = 0
    del_bad = 0

    def recycle(name, expect_file):
        nonlocal del_console_ok, del_file_ok, del_bad
        r = YomkApi.file_log_delete(name)
        if r.status == ResStatus.eOk:
            if expect_file:
                del_file_ok += 1
            else:
                del_console_ok += 1
        else:
            del_bad += 1

    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        for i in range(n):
            recycle(make_name("lg4s1_tag_", i), False)
        for i in range(n):
            recycle(make_name("lg4s2_fl_", i), True)
        for i in range(m):
            recycle(make_name("lg4s2_single_", i), True)
        recycle("lg4s3_big", True)
        for i in range(d):
            recycle(make_name("lg4s4d_solo_", i), True)
            recycle(make_name("lg4s4d_fl_", i), True)
        elapsed_del = (time.perf_counter() - t0) * 1000.0
    record_baseline("S5B_delete_all", n + n + m + 1 + 2 * d, elapsed_del)
    check(del_console_ok == n,
          f"S5B: console 组回收 eOk 守恒 {del_console_ok}/{n}")
    check(del_file_ok == n + m + 1 + 2 * d,
          f"S5B: file 组回收 eOk 守恒 {del_file_ok}/{n + m + 1 + 2 * d}")
    check(del_bad == 0, f"S5B: 全量回收无非 eOk（异常 {del_bad} 次）")

    # ---- Phase C：残量扫除（MainLogger）→ LOGGERS 行数为 0 ----
    residual_deleted = 0
    with contextlib.redirect_stdout(io.StringIO()):
        for _round in range(3):
            lines = unpack_lines(YomkApi.logger_info_loggers())
            if not lines:
                break
            for line in lines:
                name = parse_logger_name(line)
                if not name:
                    continue
                if YomkApi.file_log_delete(name).status == ResStatus.eOk:
                    residual_deleted += 1
    final_lines = unpack_lines(YomkApi.logger_info_loggers())
    check(not final_lines,
          f"S5C: 全量回收后 LOGGERS 行数为 0（残量扫除 {residual_deleted} 个，"
          f"实剩 {len(final_lines)}）")

    probes_c = n // 100 if n // 100 > 0 else 1
    gone_console = 0
    gone_file = 0
    with contextlib.redirect_stdout(io.StringIO()):
        for i in range(probes_c):
            if YomkApi.logger_info_logger(make_name("lg4s1_tag_", i)).status == ResStatus.eNo:
                gone_console += 1
            if YomkApi.logger_info_logger(make_name("lg4s2_fl_", i)).status == ResStatus.eNo:
                gone_file += 1
    check(gone_console == probes_c and gone_file == probes_c,
          f"S5C: 抽样单查全部 eNo（console {gone_console} / file {gone_file}，"
          f"共 {probes_c} 组）")

    print("[BASELINE] S5_VmHWM:" + read_vm_hwm())

    # ========================================================================
    # S6：收尾与磁盘清理
    # ========================================================================
    # 清理前磁盘足迹守恒：churn/S4D 的 .log 仍在（删除接口不删磁盘文件，清理归调用方）
    expect_disk = n + m + 1 + 2 * d + churn_total
    disk = count_disk_files(tmp_dir)
    check(disk == expect_disk,
          f"S6: 清理前磁盘 .log 文件数守恒（实测 {disk}/{expect_disk}）")

    with contextlib.redirect_stdout(io.StringIO()):
        t0 = time.perf_counter()
        YomkApi.shutdown()
        elapsed_shutdown = (time.perf_counter() - t0) * 1000.0
    print(f"[BASELINE] S6_shutdown: {elapsed_shutdown:.1f} ms"
          "（规模已在 S5 回收，析构无落盘 I/O 拖累）")

    rm_error = ""
    try:
        shutil.rmtree(tmp_dir)
    except OSError as e:
        rm_error = str(e)
    check(rm_error == "", f"S6: rmtree 无错误（{rm_error}）")
    check(not os.path.exists(tmp_dir), "S6: 临时目录已彻底清理")

    print("[BASELINE] S6_VmHWM:" + read_vm_hwm())

    if g_failed == 0:
        print("TestYomkLoggerStress all check passed.")
        return 0
    print(f"TestYomkLoggerStress FAILED ({g_failed} checks failed).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
