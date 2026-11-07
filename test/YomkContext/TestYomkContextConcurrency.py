"""
TestYomkContextConcurrency.py —— YomkContext 多线程并发功能测试与 10 万级压力测试（Context 模块 MC4）

覆盖内容：
1. 并发 get/set 共享键（m_contextsMutex 读写锁竞争）
2. 并发 create/destroy 独立键（map 结构变更）
3. 并发 set_monitor 同键（修复验证：写 monitors 在写锁内，append 无丢失/损坏）
4. 并发 set_checker × keyInfo 同键（修复验证：写 checker 与读 checker/monitors 的读写竞争）
5. 并发 set 触发 checker（checker 在写锁内串行调用）
6. 并发 set 触发 sync + async monitor（同步计数确定、异步单线程池计数确定）
7. 同步 monitor 重入安全（monitor 在锁外执行，内层 Context API 不死锁）
8. N7 压力：8 线程混合 Context 操作无崩溃/无挂起（总操作数读 YOMK_TEST_STRESS_SCALE）

语言机制说明（非功能缺口）：
- "重入 checker（checker 内调用 Context API）自死锁"为设计约束
  （checker 回调在写锁内执行），同样不构造该用例
- 并发计数用 CPython list.append 原子操作承载（GIL 下安全），
  读取用 len()
- 总操作数读 YOMK_TEST_STRESS_SCALE（缺省 100000，
  快速模式由 run_tests.sh 注入 10000，clamp 下限 1000），8 线程均分

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
门面单例拉起 /YomkContext，全程经门面 API 多线程并发路由（真实 API 路径）；
各节用独立 key 前缀，末尾 shutdown（python 门面 shutdown 后 init 不再重建）
"""

import os
import sys
import threading
import time

from YomkApi import (
    server,
    ctx,
)

from YomkServer import CheckStatus, ResStatus

g_failed = 0

def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1

def wait_for(pred, timeout_s, desc):
    # 10ms 步进轮询，超时打印 [TIMEOUT] 返回最终判定
    for _ in range(int(timeout_s / 0.01)):
        if pred():
            return True
        time.sleep(0.01)
    print(f"[TIMEOUT] {desc} (timeout {int(timeout_s * 1000)} ms)")
    return pred()

# ---- 文件级观测变量（每节首重置；计数用 list.append 原子承载，读取用 len） ----
g_sync_mon = []        # 同步 monitor 调用计数
g_async_mon = []       # 异步 monitor 调用计数
g_checker_calls = []   # checker 调用计数
g_reentrant_ok = []    # monitor 内重入 Context API 成功计数
g_illegal = []         # 非法响应计数

def reset_globals():
    g_sync_mon.clear()
    g_async_mon.clear()
    g_checker_calls.clear()
    g_reentrant_ok.clear()
    g_illegal.clear()

def main():
    server().async_thread(1).init()
    srv = server().instance()
    check(srv is not None, "init 返回非空服务器")

    # ============ Section 1: 并发 get/set 共享键（读写锁竞争）============
    print("===== 1. 并发 get/set 共享键 =====")
    reset_globals()
    resp = ctx().key("conc_rw").value("v_init").create()
    check(resp == ResStatus.eOk, "1.1 创建 conc_rw 成功")

    t_cnt, k_cnt = 8, 500
    ok_get, ok_set = [], []

    def rw_worker(t):
        for i in range(k_cnt):
            if i % 2 == 0:
                # conc_rw 始终存在且值为非空 str，命中 eOk 路径返回非空值
                v = ctx().key("conc_rw").msg().with_default("").get()
                if v:
                    ok_get.append(1)
                else:
                    g_illegal.append(1)
            else:
                r = ctx().key("conc_rw").value(f"v{t}_{i}").set()
                if r == ResStatus.eOk:
                    ok_set.append(1)
                else:
                    g_illegal.append(1)

    workers = []
    for t in range(t_cnt):
        workers.append(threading.Thread(target=rw_worker, args=(t,)))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    expect_each = t_cnt * (k_cnt // 2)
    check(len(ok_get) == expect_each, "1.2 并发 get 全部 eOk")
    check(len(ok_set) == expect_each, "1.3 并发 set 全部 eOk")
    check(len(g_illegal) == 0, "1.4 无非法响应")

    # ============ Section 2: 并发 create/destroy 独立键（map 结构变更）============
    print("===== 2. 并发 create/destroy 独立键 =====")
    reset_globals()
    t_cnt, k_cnt = 8, 200
    ok_create, ok_destroy = [], []

    def cd_worker(t):
        key = f"conc_cd_{t}"
        for _ in range(k_cnt):
            c = ctx().key(key).value("x").create()
            if c == ResStatus.eOk:
                ok_create.append(1)
            else:
                g_illegal.append(1)
            d = ctx().key(key).destroy()
            if d == ResStatus.eOk:
                ok_destroy.append(1)
            else:
                g_illegal.append(1)

    workers = []
    for t in range(t_cnt):
        workers.append(threading.Thread(target=cd_worker, args=(t,)))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(ok_create) == t_cnt * k_cnt, "2.1 并发 create 全部 eOk")
    check(len(ok_destroy) == t_cnt * k_cnt, "2.2 并发 destroy 全部 eOk")
    check(len(g_illegal) == 0, "2.3 无非法响应")

    # 各专属键最终均已销毁（无泄漏）
    all_gone = all(ctx().key(f"conc_cd_{t}").info().status == ResStatus.eNo
                   for t in range(t_cnt))
    check(all_gone, "2.4 独立键全部销毁，keys 无残留")

    # ============ Section 3: 并发 set_monitor 同键（修复验证）============
    print("===== 3. 并发 set_monitor 同键 =====")
    reset_globals()
    resp = ctx().key("conc_mon").value("m0").create()
    check(resp == ResStatus.eOk, "3.1 创建 conc_mon 成功")
    ctx().monitor().enable()

    t_cnt, k_cnt = 8, 100  # 共 t_cnt*k_cnt 个同步 monitor 并发 append
    ok_mon = []

    def mon_worker():
        for _ in range(k_cnt):
            r = ctx().key("conc_mon").monitor_fn(lambda c: g_sync_mon.append(1)).apply()
            if r == ResStatus.eOk:
                ok_mon.append(1)
            else:
                g_illegal.append(1)

    workers = []
    for _ in range(t_cnt):
        workers.append(threading.Thread(target=mon_worker))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(ok_mon) == t_cnt * k_cnt, "3.2 并发 set_monitor 全部 eOk")
    check(len(g_illegal) == 0, "3.3 无非法响应")

    # 单次 set 触发全部已注册 monitor：计数 == 注册次数，证明并发 append 无丢失/损坏
    g_sync_mon.clear()
    sr = ctx().key("conc_mon").value("m1").set()
    check(sr == ResStatus.eOk, "3.4 触发 set 返回 eOk")
    check(len(g_sync_mon) == t_cnt * k_cnt,
          "3.5 并发注册的 monitor 全部落地（计数 == 注册次数）")
    ctx().monitor().disable()

    # ============ Section 4: 并发 set_checker × keyInfo 同键（修复验证读写竞争）============
    print("===== 4. 并发 set_checker x keyInfo 同键 =====")
    reset_globals()
    resp = ctx().key("conc_ck").value("c0").create()
    check(resp == ResStatus.eOk, "4.1 创建 conc_ck 成功")

    t_cnt, k_cnt = 16, 100  # 半数写 checker、半数读 keyInfo
    ok_writer, ok_reader = [], []

    def ck_worker(writer):
        for _ in range(k_cnt):
            if writer:
                r = ctx().key("conc_ck").checker_fn(lambda c: CheckStatus.eAccept).apply()
                if r == ResStatus.eOk:
                    ok_writer.append(1)
                else:
                    g_illegal.append(1)
            else:
                r = ctx().key("conc_ck").info()
                if r.status == ResStatus.eOk:
                    ok_reader.append(1)
                else:
                    g_illegal.append(1)

    workers = []
    for t in range(t_cnt):
        workers.append(threading.Thread(target=ck_worker, args=(t % 2 == 0,)))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(ok_writer) == (t_cnt // 2) * k_cnt, "4.2 并发 set_checker 全部 eOk")
    check(len(ok_reader) == (t_cnt // 2) * k_cnt, "4.3 并发 keyInfo 全部 eOk")
    check(len(g_illegal) == 0, "4.4 读写并发无非法响应、无崩溃")

    # 并发注册后 checker 仍有效：开启 checker 后单次 set 调用 checker 一次
    ctx().checker().enable()
    g_checker_calls.clear()
    ctx().key("conc_ck").checker_fn(lambda c: (g_checker_calls.append(1), CheckStatus.eAccept)[1]).apply()
    sr = ctx().key("conc_ck").value("c1").set()
    check(sr == ResStatus.eOk, "4.5 并发注册后 set 仍 eOk")
    check(len(g_checker_calls) == 1, "4.6 checker 有效并被调用 1 次")
    ctx().checker().disable()

    # ============ Section 5: 并发 set 触发 checker ============
    print("===== 5. 并发 set 触发 checker =====")
    reset_globals()
    resp = ctx().key("conc_setck").value("s0").create()
    check(resp == ResStatus.eOk, "5.1 创建 conc_setck 成功")
    ctx().key("conc_setck").checker_fn(lambda c: (g_checker_calls.append(1), CheckStatus.eAccept)[1]).apply()
    ctx().checker().enable()

    t_cnt, k_cnt = 8, 200
    ok_set = []
    g_checker_calls.clear()

    def setck_worker():
        for i in range(k_cnt):
            r = ctx().key("conc_setck").value(f"s{i}").set()
            if r == ResStatus.eOk:
                ok_set.append(1)
            else:
                g_illegal.append(1)

    workers = []
    for _ in range(t_cnt):
        workers.append(threading.Thread(target=setck_worker))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(ok_set) == t_cnt * k_cnt, "5.2 并发 set 全部 eOk")
    check(len(g_checker_calls) == t_cnt * k_cnt,
          "5.3 checker 调用次数 == set 次数（写锁内串行）")
    check(len(g_illegal) == 0, "5.4 无非法响应")
    ctx().checker().disable()

    # ============ Section 6: 并发 set 触发 sync + async monitor ============
    print("===== 6. 并发 set 触发 sync+async monitor =====")
    reset_globals()
    resp = ctx().key("conc_setmon").value("n0").create()
    check(resp == ResStatus.eOk, "6.1 创建 conc_setmon 成功")
    ctx().key("conc_setmon").monitor_fn(lambda c: g_sync_mon.append(1)).apply()
    ctx().key("conc_setmon").monitor_fn(lambda c: g_async_mon.append(1)).async_(True).apply()
    ctx().monitor().enable()

    t_cnt, k_cnt = 8, 200
    ok_set = []
    g_sync_mon.clear()
    g_async_mon.clear()

    def setmon_worker():
        for i in range(k_cnt):
            r = ctx().key("conc_setmon").value(f"n{i}").set()
            if r == ResStatus.eOk:
                ok_set.append(1)
            else:
                g_illegal.append(1)

    workers = []
    for _ in range(t_cnt):
        workers.append(threading.Thread(target=setmon_worker))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(ok_set) == t_cnt * k_cnt, "6.2 并发 set 全部 eOk")
    check(len(g_sync_mon) == t_cnt * k_cnt, "6.3 同步 monitor 计数 == set 次数")
    async_ok = wait_for(lambda: len(g_async_mon) == t_cnt * k_cnt, 10.0,
                        "async monitor count")
    check(async_ok, "6.4 异步 monitor 计数 == set 次数（单线程池排空）")
    check(len(g_illegal) == 0, "6.5 无非法响应")
    ctx().monitor().disable()

    # ============ Section 7: 同步 monitor 重入安全（锁外执行正面验证）============
    print("===== 7. 同步 monitor 重入安全 =====")
    reset_globals()
    cra = ctx().key("conc_a").value("a0").create()
    crb = ctx().key("conc_b").value("b0").create()
    check(cra == ResStatus.eOk and crb == ResStatus.eOk,
          "7.1 创建 conc_a/conc_b 成功")

    # conc_a 的同步 monitor 内重入 Context API（读 conc_b）：set 已在锁外执行
    # monitor，不应死锁
    def reentrant_monitor(ctx_obj):
        r = ctx().key("conc_b").info()
        if r.status == ResStatus.eOk:
            g_reentrant_ok.append(1)

    ctx().key("conc_a").monitor_fn(reentrant_monitor).apply()
    ctx().monitor().enable()

    g_reentrant_ok.clear()
    sr = ctx().key("conc_a").value("a1").set()
    check(sr == ResStatus.eOk, "7.2 set(conc_a) 返回 eOk（monitor 重入未死锁）")
    check(len(g_reentrant_ok) == 1, "7.3 monitor 内重入 INFO_KEY(conc_b) 成功 1 次")
    ctx().monitor().disable()

    # ============ Section 8: N7 压力（8 线程混合操作）============
    print("===== 8. N7 压力（≈10 万级混合操作） =====")
    reset_globals()
    m_keys = 64  # 预建 keyspace 大小
    for n in range(m_keys):
        cr = ctx().key(f"stress_{n}").value(f"init{n}").create()
        if cr != ResStatus.eOk:
            g_illegal.append(1)
    check(len(g_illegal) == 0, "8.1 预建 keyspace 成功")

    # 压力段关闭 checker/monitor：避免单线程监控池积压，聚焦锁并发与 map 一致性
    ctx().checker().disable()
    ctx().monitor().disable()

    # 总操作数：YOMK_TEST_STRESS_SCALE 缺省 100000（10 万级，8 线程均分）/
    # 快速模式 10000，clamp 下限 1000；8 线程均分
    total_ops = int(os.environ.get("YOMK_TEST_STRESS_SCALE", "100000"))
    total_ops = max(total_ops, 1000)
    t_cnt = 8
    k_cnt = total_ops // t_cnt
    ok_ops = []

    def stress_worker(t):
        for i in range(k_cnt):
            key = f"stress_{i % m_keys}"
            op = i % 8
            if op == 0:
                v = ctx().key(key).msg().with_default("").get()
                if v:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 1:
                r = ctx().key(key).value("sv").set()
                if r == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 2:
                r = ctx().keys()
                if r.status == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 3:
                r = ctx().key(key).info()
                if r.status == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 4:
                r = ctx().all()
                if r.status == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 5:
                r = ctx().key(key).checker_fn(lambda c: CheckStatus.eAccept).apply()
                if r == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            elif op == 6:
                r = ctx().key(key).monitor_fn(lambda c: None).apply()
                if r == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)
            else:
                tk = f"stress_tmp_{t}"
                c = ctx().key(tk).value("tmp").create()
                d = ctx().key(tk).destroy()
                if c == ResStatus.eOk and d == ResStatus.eOk:
                    ok_ops.append(1)
                else:
                    g_illegal.append(1)

    workers = []
    for t in range(t_cnt):
        workers.append(threading.Thread(target=stress_worker, args=(t,)))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    check(len(g_illegal) == 0, "8.2 混合操作无非法响应")
    check(len(ok_ops) == t_cnt * k_cnt, "8.3 全部操作成功计数守恒")

    # 最终一致性：keyspace 仍存在、临时键已销毁
    keyspace_ok = (ctx().key("stress_0").info().status == ResStatus.eOk
                   and ctx().key(f"stress_{m_keys - 1}").info().status == ResStatus.eOk)
    check(keyspace_ok, "8.4 压力后 keyspace 仍存在")
    tmp_gone = all(ctx().key(f"stress_tmp_{t}").info().status == ResStatus.eNo
                   for t in range(t_cnt))
    check(tmp_gone, "8.5 压力后临时键全部销毁")

    server().shutdown()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
