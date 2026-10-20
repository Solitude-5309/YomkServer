"""
TestYomkEventLoopStress.py - EventLoop 压力测试（10 万级投递 + 大数据量 + QPS 基线记录）

覆盖内容（5 个 Section，规模 N 经 YOMK_TEST_STRESS_SCALE 环境变量参数化，缺省 100000）：
1. 单线程 10 万级投递基线：post N 个 trivial 计数事件 -> drain，计数守恒 + 入队/消费 QPS 基线记录
2. 8 线程并发 10 万级：并发 post -> drain，计数守恒 + 聚合 QPS 基线（高并发持续调用无崩溃）
3. 10 万级纯积压路径：gate 占住 worker 使投递/消费速率解耦——纯测 post + 无界队列大规模增长
   （断言 pending:N 积压确认，释放后守恒）
4. 大数据量字符串：4096 字节 tag × max(N/10,1000) 个事件，无崩溃且守恒
5. API 层 10 万级投递：event_loop_post 走完整路由路径，计数守恒 + API 级 QPS 基线

说明：
- QPS/耗时基线仅以 [BASELINE] 行记录、不作断言（环境噪声使性能断言不可靠）；
- 计数 handler 为 GIL 原子承载（list.append/len，见 Concurrency docstring 语言机制说明）；
  计时用 time.perf_counter；
- 白盒段直连内部类 EventLoop；API 段 YomkApi.init 单例，末尾 YomkApi.shutdown 收尾。

风格：脚本式 main + 失败计数，退出码 0/1（零第三方依赖）
"""

import os
import sys
import threading
import time

import YomkServerPy as ysp
from YomkServerPy import Event, EventLoop

FAILED = 0

# ---- 文件级观测装置（GIL 原子映射）----
EXEC_COUNT = []  # 计数 handler 执行总数


def count_handler():
    def _handler(pkg):
        EXEC_COUNT.append(1)
        return ysp.YomkResponse(ysp.ResStatus.eOk, "counted")
    return _handler


# ---- 文件级阻塞门：占住工作线程使投递/消费速率解耦 ----
class Gate:
    def __init__(self):
        self.started = threading.Event()
        self.release = threading.Event()

    def reset(self):
        self.started.clear()
        self.release.clear()


GATE = Gate()


def gate_handler():
    def _handler(pkg):
        GATE.started.set()
        GATE.release.wait()
        return ysp.YomkResponse(ysp.ResStatus.eOk, "gate done")
    return _handler


def wait_gate_started(timeout_s):
    return GATE.started.wait(timeout_s)


def open_gate():
    GATE.release.set()


def wait_until(pred, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if pred():
            return True
        time.sleep(0.001)
    return pred()


def check(cond, msg):
    global FAILED
    if not cond:
        print("[FAIL]", msg)
        FAILED += 1
    else:
        print("[ OK ]", msg)


def stress_scale():
    # 环境变量 YOMK_TEST_STRESS_SCALE（run_tests.sh 快速模式降规模用），缺省 10 万，clamp 下限 1000
    env = os.environ.get("YOMK_TEST_STRESS_SCALE")
    if not env:
        return 100000
    try:
        v = int(env)
    except ValueError:
        return 100000
    return max(v, 1000)


def record_baseline(tag, ops, elapsed_ms):
    # 基线记录（仅打印，不断言）：耗时(ms) 与吞吐(ops/s)
    qps = ops * 1000.0 / elapsed_ms if elapsed_ms > 0 else 0.0
    print(
        f"[BASELINE] {tag} ops={ops} elapsed_ms={elapsed_ms} throughput={int(qps)}/s")


# ============ Section 1: 单线程 10 万级投递基线（白盒）============
def section_1(n):
    del EXEC_COUNT[:]
    loop = EventLoop()
    check(loop.start() == 0, "S1 start 返回 0")

    t0 = time.perf_counter()
    for _k in range(n):
        loop.post(Event("st1", None, count_handler(), ""))
    t1 = time.perf_counter()
    drained = wait_until(
        lambda: len(EXEC_COUNT) == n and "pending:0" in loop.info_line("st1", 0), 30.0)
    t2 = time.perf_counter()

    check(drained, "S1 drain 完成（30s 看门狗）")
    check(len(EXEC_COUNT) == n, "S1 计数守恒（N/N，无丢失无重复）")
    record_baseline("S1 single-thread post", n, int((t1 - t0) * 1000))
    record_baseline("S1 single-thread drain", n, int((t2 - t1) * 1000))

    loop.destroy()


# ============ Section 2: 8 线程并发 10 万级（白盒）============
def section_2(n):
    del EXEC_COUNT[:]
    loop = EventLoop()
    check(loop.start() == 0, "S2 start 返回 0")

    t0 = time.perf_counter()

    def poster(per_thread):
        for _k in range(per_thread):
            loop.post(Event("st2", None, count_handler(), ""))

    per_thread = n // 8
    posters = [threading.Thread(target=poster, args=(per_thread,))
               for _ in range(8)]
    for th in posters:
        th.start()
    for th in posters:
        th.join()
    t1 = time.perf_counter()
    drained = wait_until(
        lambda: len(EXEC_COUNT) == n and "pending:0" in loop.info_line("st2", 0), 30.0)
    t2 = time.perf_counter()

    check(drained, "S2 drain 完成（30s 看门狗）")
    check(len(EXEC_COUNT) == n, "S2 并发计数守恒（8 线程 N/N）")
    record_baseline("S2 8-thread concurrent post", n, int((t1 - t0) * 1000))
    record_baseline("S2 8-thread drain", n, int((t2 - t1) * 1000))

    loop.destroy()


# ============ Section 3: 10 万级纯积压路径（白盒，gate 解耦投递/消费）============
def section_3(n):
    del EXEC_COUNT[:]
    GATE.reset()
    loop = EventLoop()
    check(loop.start() == 0, "S3 start 返回 0")

    # gate 占住 worker：投递速率与消费速率解耦，纯测 post + 无界队列大规模增长
    check(loop.post(Event("st3", None, gate_handler(), "gate"))
          == 0, "S3 post gate 事件返回 0")
    check(wait_gate_started(2.0), "S3 gate 已占住工作线程")

    t0 = time.perf_counter()
    for _k in range(n):
        loop.post(Event("st3", None, count_handler(), ""))
    t1 = time.perf_counter()

    line = loop.info_line("st3", 0)
    check(f"pending:{n}" in line, f"S3 积压确认（pending:{n}，无界队列大规模增长无崩溃）")

    open_gate()
    drained = wait_until(
        lambda: len(EXEC_COUNT) == n and "pending:0" in loop.info_line("st3", 0), 30.0)
    t2 = time.perf_counter()

    check(drained, "S3 释放后 drain 完成（30s 看门狗）")
    check(len(EXEC_COUNT) == n, "S3 积压后计数守恒（N/N）")
    record_baseline("S3 backlog-only post (gate held)",
                    n, int((t1 - t0) * 1000))
    record_baseline("S3 backlog drain after release", n, int((t2 - t1) * 1000))

    loop.destroy()


# ============ Section 4: 大数据量字符串（白盒）============
def section_4(n):
    del EXEC_COUNT[:]
    loop = EventLoop()
    check(loop.start() == 0, "S4 start 返回 0")

    tag_len = 4096
    count = n // 10 if n // 10 >= 1000 else 1000
    big_tag = "T" * tag_len

    t0 = time.perf_counter()
    for _k in range(count):
        loop.post(Event("st4", None, count_handler(), big_tag))
    t1 = time.perf_counter()
    drained = wait_until(
        lambda: len(EXEC_COUNT) == count and "pending:0" in loop.info_line("st4", 0), 30.0)
    t2 = time.perf_counter()

    check(drained, "S4 drain 完成（30s 看门狗）")
    check(len(EXEC_COUNT) == count, "S4 大数据量计数守恒（无崩溃无丢失）")
    record_baseline(
        f"S4 big-tag post ({count} x {tag_len}B)", count, int((t1 - t0) * 1000))
    record_baseline("S4 big-tag drain", count, int((t2 - t1) * 1000))

    loop.destroy()


# ============ Section 5: API 层 10 万级投递 ============
def section_5(n):
    import YomkApi
    del EXEC_COUNT[:]
    server = YomkApi.init(1)
    check(server is not None, "S5 YomkApi.init 返回非空服务器")
    check(YomkApi.event_loop_start("stress_loop").status ==
          ysp.ResStatus.eOk, "S5 START 返回 eOk")

    t0 = time.perf_counter()
    for _k in range(n):
        YomkApi.event_loop_post("stress_loop", "payload", count_handler(), "")
    t1 = time.perf_counter()
    drained = wait_until(lambda: len(EXEC_COUNT) == n, 30.0)
    t2 = time.perf_counter()

    check(drained, "S5 drain 完成（30s 看门狗）")
    check(len(EXEC_COUNT) == n, "S5 API 层计数守恒（N/N，路由路径不丢失）")
    record_baseline("S5 API-level post (event_loop_post)",
                    n, int((t1 - t0) * 1000))
    record_baseline("S5 API-level end-to-end", n, int((t2 - t0) * 1000))

    check(YomkApi.event_loop_destroy("stress_loop").status ==
          ysp.ResStatus.eOk, "S5 DESTROY 返回 eOk")
    YomkApi.shutdown()


def main():
    n = stress_scale()
    print(f"stress scale N = {n}")

    section_1(n)
    section_2(n)
    section_3(n)
    section_4(n)
    section_5(n)

    print("TestYomkEventLoopStress all check passed." if FAILED == 0
          else f"TestYomkEventLoopStress FAILED ({FAILED} checks failed).")
    sys.exit(0 if FAILED == 0 else 1)


if __name__ == "__main__":
    main()
