"""
TestYomkEventLoopConcurrency.py - EventLoop 多线程并发正确性测试（白盒 + API 层）

覆盖内容（5 个 Section）：
1. 并发 post 保序 + 计数守恒：8 线程×40 事件并发投递，执行数==320 且执行序==eventId 严格递增序
   （单 worker FIFO + 锁内赋号入队，赋号序=入队序=执行序三序合一；严格递增蕴含 eventId 全局唯一）
2. 多 loop 并行：loopA 的 worker 被阻塞门占住期间，loopB 事件仍完成——不同 loop 独立 worker 的确定性证明
3. 并发 postWait：8 线程各 10 次同步投递全部看门狗内返回、响应已填充、总数守恒
4. start/stop/post/postWait 混合 churn 无死锁：固定迭代计数（非墙钟时长），收尾 drain 后精确计数守恒
   executed==accepted（rc==0 必已入队、stop 保留、最终执行）
5. API 层并发投递 + 内省轮询：4 线程并发 POST + 1 线程并发 INFO_LOOP（服务层读路径稳定性）

设计边界（契约内使用，非缺陷）：
- churn 仅单线程做 start/stop——start/stop 线程安全契约为调用方串行化（并发
  start×2 的 worker 置换有终止线程风险，以调用方串行化为契约）；
- churn 不含 destroy——丢弃已接受事件会破坏计数守恒语义，其等待者释放已由 Lifecycle Section 10 覆盖。

语言机制说明：并发计数用 list.append/len（GIL 单步原子，免锁）；原子自旋门以 threading.Event 承载；
事件执行序列记录 -> list append 一次性构造快照；计数 handler 闭包持事件对象引用记 m_eventId
（闭包直接持引用，无悬垂风险）。

风格：脚本式 main + 失败计数，退出码 0/1（零第三方依赖）
"""

import sys
import threading
import time

import YomkServerPy as ysp
from YomkServerPy import Event, EventLoop

FAILED = 0

# ---- 文件级观测装置（GIL 原子映射：list.append/len 单步原子，免锁）----
EXEC_COUNT = []   # 计数 handler 执行总数（S2/S3/S4/S5）
EXEC_IDS = []     # S1 专用：事件执行时的 eventId（handler 闭包持事件引用）


def count_handler():
    def _handler(pkg):
        EXEC_COUNT.append(1)
        return ysp.YomkResponse(ysp.ResStatus.eOk, "counted")
    return _handler


# ---- 文件级阻塞门：占住指定 loop 的工作线程制造确定性窗口 ----
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


# ============ Section 1: 并发 post 保序 + 计数守恒（白盒，单 loop）============
def section_1():
    del EXEC_IDS[:]
    loop = EventLoop()
    check(loop.start() == 0, "S1 start 返回 0")

    post_ok = [0]
    post_ok_lock = threading.Lock()

    def poster(_):
        for _k in range(40):
            ev = Event("conc1", None, None, "")
            ev.m_serviceFunc = (lambda e: (
                lambda pkg: EXEC_IDS.append(e.m_eventId)))(ev)
            if loop.post(ev) == 0:
                with post_ok_lock:
                    post_ok[0] += 1

    posters = [threading.Thread(target=poster, args=(t,)) for t in range(8)]
    for th in posters:
        th.start()
    for th in posters:
        th.join()
    check(post_ok[0] == 320, "S1 并发 post 全部返回 0（320/320）")

    check(wait_until(lambda: "pending:0" in loop.info_line(
        "conc1", 0), 5.0), "S1 队列排空（drain 完成）")

    ids = list(EXEC_IDS)
    check(len(ids) == 320, "S1 计数守恒：执行数==投递数（320/320，无丢失无重复）")
    strictly_increasing = all(ids[i] > ids[i - 1] for i in range(1, len(ids)))
    check(strictly_increasing, "S1 执行序==eventId 严格递增序（并发提交下 FIFO 保序且赋号全局唯一）")

    loop.destroy()


# ============ Section 2: 多 loop 并行（白盒，确定性）============
def section_2():
    del EXEC_COUNT[:]
    GATE.reset()
    loop_a = EventLoop()
    loop_b = EventLoop()
    check(loop_a.start() == 0, "S2 loopA start 返回 0")
    check(loop_b.start() == 0, "S2 loopB start 返回 0")

    # loopA 的 worker 被阻塞门占住
    check(loop_a.post(Event("pa", None, gate_handler(), "gate"))
          == 0, "S2 loopA post 阻塞事件返回 0")
    check(wait_gate_started(2.0), "S2 阻塞门已占住 loopA 的工作线程")

    # loopB 的事件在 A 阻塞期间完成——若两 loop 共享线程则不可能，此为并行性的确定性证明
    for _k in range(5):
        check(loop_b.post(Event("pb", None, count_handler(), ""))
              == 0, "S2 loopB post 计数事件返回 0")
    check(wait_until(lambda: len(EXEC_COUNT) == 5, 2.0),
          "S2 loopB 事件在 loopA 阻塞期间全部完成（独立 worker 并行）")

    # 释放 loopA：gate 完成后 A 的后续事件恢复执行
    open_gate()
    check(loop_a.post(Event("pa", None, count_handler(), ""))
          == 0, "S2 loopA post 后续计数事件返回 0")
    check(wait_until(lambda: len(EXEC_COUNT) == 6, 2.0),
          "S2 释放后 loopA 恢复执行（gate 完成+后续事件）")
    check(len(EXEC_COUNT) == 6, "S2 两 loop 计数守恒（5+1==6）")

    loop_a.destroy()
    loop_b.destroy()


# ============ Section 3: 并发 postWait（白盒）============
def section_3():
    del EXEC_COUNT[:]
    loop = EventLoop()
    check(loop.start() == 0, "S3 start 返回 0")

    waiter_done = [0]     # 完成线程数
    wait_rc_ok = [0]      # postWait 返回 0 的次数
    wait_resp_ok = [0]    # 返回时响应已填充 eOk 的次数
    stat_lock = threading.Lock()

    def waiter():
        for _k in range(10):
            ev = Event("conc3", None, count_handler(), "")
            rc = loop.post_wait(ev)
            with stat_lock:
                if rc == 0:
                    wait_rc_ok[0] += 1
                if ev.m_response is not None and ev.m_response.status == ysp.ResStatus.eOk:
                    wait_resp_ok[0] += 1
        with stat_lock:
            waiter_done[0] += 1

    waiters = [threading.Thread(target=waiter) for _ in range(8)]
    for th in waiters:
        th.start()

    check(wait_until(lambda: waiter_done[0] ==
          8, 5.0), "S3 全部 8 线程看门狗内完成（postWait 无挂起）")
    for th in waiters:
        th.join()

    check(wait_rc_ok[0] == 80, "S3 全部 postWait 返回 0（80/80）")
    check(wait_resp_ok[0] == 80, "S3 全部返回时响应已填充 eOk（80/80）")
    check(len(EXEC_COUNT) == 80, "S3 总执行数守恒（80/80）")
    check("pending:0" in loop.info_line("conc3", 0), "S3 完成后 pending:0")

    loop.destroy()


# ============ Section 4: start/stop/post/postWait 混合 churn 无死锁（白盒）============
def section_4():
    del EXEC_COUNT[:]
    loop = EventLoop()
    check(loop.start() == 0, "S4 start 返回 0")

    accepted_post = [0]  # post 返回 0（已入队，必执行）
    accepted_wait = [0]  # postWait 返回 0（已入队，必执行）
    stat_lock = threading.Lock()

    def poster():
        for _k in range(500):
            if loop.post(Event("conc4", None, count_handler(), "")) == 0:
                with stat_lock:
                    accepted_post[0] += 1

    def waiter():
        for _k in range(100):
            # 停止态返回 2 被拒：事件未入队不计数，直接投下一个
            if loop.post_wait(Event("conc4", None, count_handler(), "")) == 0:
                with stat_lock:
                    accepted_wait[0] += 1

    # 单 churn 线程做 start/stop（契约：调用方串行化；不含 destroy——丢弃会破坏守恒语义）
    def churner():
        for _r in range(40):
            loop.stop()
            time.sleep(0.01)
            loop.start()

    workers = [threading.Thread(target=poster), threading.Thread(target=poster),
               threading.Thread(target=waiter), threading.Thread(
                   target=waiter),
               threading.Thread(target=churner)]
    for th in workers:
        th.start()
    for th in workers:
        th.join()  # 死锁将表现为 join 卡死，由 run_tests.sh 超时兜底判 FAIL

    check(loop.start() == 0, "S4 churn 结束后确保运行态（幂等）")
    expected = accepted_post[0] + accepted_wait[0]
    check(wait_until(
        lambda: len(EXEC_COUNT) == expected and "pending:0" in loop.info_line("conc4", 0), 5.0),
        "S4 churn 后队列排空（drain 完成）")
    check(len(EXEC_COUNT) == expected,
          "S4 精确计数守恒：executed==accepted（rc==0 必入队、stop 保留、最终执行）")
    check(expected > 0, "S4 混合期间存在成功投递（churn 未饿死投递路径）")

    loop.destroy()


# ============ Section 5: API 层并发投递 + 内省轮询 ============
def section_5():
    import YomkApi
    del EXEC_COUNT[:]
    server = YomkApi.init(1)
    check(server is not None, "S5 YomkApi.init 返回非空服务器")

    check(YomkApi.event_loop_start("conc_loop").status ==
          ysp.ResStatus.eOk, "S5 START conc_loop 返回 eOk")

    api_post_ok = [0]
    poll_count = [0]
    poll_ok = [0]
    stop_poll = threading.Event()
    stat_lock = threading.Lock()
    poll_fail_samples = []

    def api_poster():
        for _k in range(25):
            if YomkApi.event_loop_post("conc_loop", "x", count_handler(), "").status == ysp.ResStatus.eOk:
                with stat_lock:
                    api_post_ok[0] += 1

    def poller():
        # 下限 50 次：高负载下轮询线程可能在投递完成前未被调度（启动饥饿），
        # 投递完成后补足最少轮询数；轮询窗口仍覆盖并发投递期
        while not stop_poll.is_set() or poll_count[0] < 50:
            r = YomkApi.event_loop_info_loop("conc_loop")
            with stat_lock:
                poll_count[0] += 1
                if r.status == ysp.ResStatus.eOk and "running:on" in r.msg:
                    poll_ok[0] += 1
                elif len(poll_fail_samples) < 5:  # 诊断：留存首批失败样本
                    poll_fail_samples.append(
                        f"status={r.status} msg=[{r.msg}]")

    posters = [threading.Thread(target=api_poster) for _ in range(4)]
    poll_thread = threading.Thread(target=poller)
    for th in posters:
        th.start()
    poll_thread.start()
    for th in posters:
        th.join()
    stop_poll.set()
    poll_thread.join()

    check(api_post_ok[0] == 100, "S5 并发 POST 全部 eOk（100/100）")
    check(poll_count[0] >= 50 and poll_ok[0] == poll_count[0],
          "S5 并发轮询 INFO_LOOP 全部 eOk 且 running:on（polls=%d ok=%d 首个失败样本: %s）"
          % (poll_count[0], poll_ok[0], poll_fail_samples[0] if poll_fail_samples else "无"))
    check(wait_until(lambda: len(EXEC_COUNT) ==
          100, 5.0), "S5 API 层事件全部执行（100/100）")

    check(YomkApi.event_loop_destroy("conc_loop").status ==
          ysp.ResStatus.eOk, "S5 DESTROY conc_loop 返回 eOk")
    YomkApi.shutdown()


def main():
    section_1()
    section_2()
    section_3()
    section_4()
    section_5()

    print("TestYomkEventLoopConcurrency all check passed." if FAILED == 0
          else f"TestYomkEventLoopConcurrency FAILED ({FAILED} checks failed).")
    sys.exit(0 if FAILED == 0 else 1)


if __name__ == "__main__":
    main()
