"""
TestYomkFunctionPoolConcurrency.py —— YomkFunctionPool 并发正确性验证（5 节 26 断言）

节结构逐节对齐同名 C++ 测试（S1 并发 CALL 守恒 + 嵌套重入 / S2 REGISTER 更新 x CALL
写读交错 / S3 UNREGISTER x CALL 锁外安全性 / S4 内省 x 变更（INFO_ALL 结构化快照持续可解包）/ S5 全量 churn
混合守恒），零剔除。

语言映射：
- YOMK_FUNCTIONPOOL_* 宏 → 门面 function_pool_* 六 API（覆盖 invoke 路由链）
- std::atomic 计数 → list.append（GIL 原子，len() 读数）
- 自旋门/停止标志 → threading.Event（S3 started/release gate、S2 stop）
- SnapshotRecord + std::mutex → list.append(条目数)
  （GIL 下 append 原子，免锁）
- 装置全部走门面 API（对齐宏形态，覆盖 invoke 路由链）
"""

import sys
import threading
import time

from YomkApi import (
    ResStatus,
    YomkResponse,
    function_pool_call,
    function_pool_info_all,
    function_pool_info_names,
    function_pool_register,
    function_pool_unregister,
    init,
)

PASS = 0
FAIL = 0


def check(cond, name):
    """断言计数：[ OK ] / [FAIL] 行形态与全库测试一致。"""
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"[ OK ] {name}")
    else:
        FAIL += 1
        print(f"[FAIL] {name}")


# ============================================================================
# 装置函数（std::atomic 计数 → list.append，GIL 原子）
# ============================================================================

# S1 守恒
s1_exec = []
s1_ok = []
s1_inner = []
s1_outer_ok = []


def s1_probe_func(pkg):
    s1_exec.append(1)
    return YomkResponse(ResStatus.eOk, "s1_ok")


def s1_inner_func(pkg):
    s1_inner.append(1)
    return YomkResponse(ResStatus.eOk, "s1_inner_ok")


def s1_outer_func(pkg):
    # 嵌套重入：在已注册函数内部再 CALL 另一个已注册函数
    # callFunction 的读锁已释放（锁外调用），此处重新获取读锁安全
    resp = function_pool_call("c1_inner", None)
    if resp.status == ResStatus.eOk:
        s1_outer_ok.append(1)
    return resp


# S2 守恒
s2_v1 = []
s2_v2 = []
s2_total = []


def s2_v1_func(pkg):
    s2_v1.append(1)
    return YomkResponse(ResStatus.eOk, "v1")


def s2_v2_func(pkg):
    s2_v2.append(1)
    return YomkResponse(ResStatus.eOk, "v2")


# S3 锁外调用安全性（gate 模式：函数体内等释放，制造"锁外执行中"确定窗口）
s3_exec = []
s3_gate_started = threading.Event()
s3_gate_release = threading.Event()


def s3_gated_func(pkg):
    s3_exec.append(1)
    # 此时 callFunction 的读锁已释放，本函数在锁外执行——主线程可安全获取写锁做 unregister
    s3_gate_started.set()
    s3_gate_release.wait()
    return YomkResponse(ResStatus.eOk, "s3_gated_done")


# S5 用函数
def s5_probe_func(pkg):
    return YomkResponse(ResStatus.eOk, "s5_ok")


def main():
    global PASS, FAIL
    print("=== TestYomkFunctionPoolConcurrency ===")

    server = init(1)
    check(server is not None, "init(1) 返回 server 实例")

    # ========================================================================
    # S1: 并发 CALL 守恒 + 嵌套重入（只读路径）
    # ========================================================================
    print("\n--- S1: 并发 CALL 守恒 + 嵌套重入 ---")

    k_threads = 8
    k_calls_per_thread = 500
    k_total = k_threads * k_calls_per_thread

    r = function_pool_register("c1_probe", s1_probe_func)
    check(r.status == ResStatus.eOk, "S1: 注册 c1_probe 成功")

    # 多线程并发 CALL（读锁并发读 + 函数体取出）
    callers = []
    for _ in range(k_threads):
        def caller():
            for _ in range(k_calls_per_thread):
                resp = function_pool_call("c1_probe", None)
                if resp.status == ResStatus.eOk:
                    s1_ok.append(1)
        t = threading.Thread(target=caller)
        callers.append(t)
        t.start()
    for t in callers:
        t.join()

    check(len(s1_exec) == k_total,
          f"S1: 执行守恒 execCount == {k_total}（实际 {len(s1_exec)}）")
    check(len(s1_ok) == k_total, f"S1: 响应守恒 okCount == {k_total}")

    # 嵌套重入验证
    k_nested_threads = 4
    k_nested_calls = 100
    k_nested_total = k_nested_threads * k_nested_calls

    r = function_pool_register("c1_inner", s1_inner_func)
    check(r.status == ResStatus.eOk, "S1: 注册 c1_inner 成功")
    r = function_pool_register("c1_outer", s1_outer_func)
    check(r.status == ResStatus.eOk, "S1: 注册 c1_outer（内部嵌套 CALL c1_inner）成功")

    nested = []
    for _ in range(k_nested_threads):
        def nested_caller():
            for _ in range(k_nested_calls):
                function_pool_call("c1_outer", None)
        t = threading.Thread(target=nested_caller)
        nested.append(t)
        t.start()
    for t in nested:
        t.join()

    check(len(s1_inner) == k_nested_total,
          f"S1: 嵌套重入守恒 innerCount == {k_nested_total}（实际 {len(s1_inner)}）——同线程二次读锁无死锁")
    check(len(s1_outer_ok) == k_nested_total,
          "S1: 嵌套重入 outer 全部成功回传 inner 结果")

    # 清理
    function_pool_unregister("c1_probe")
    function_pool_unregister("c1_inner")
    function_pool_unregister("c1_outer")
    r = function_pool_call("c1_probe", None)
    check(r.status == ResStatus.eNo, "S1: 清理后 c1_probe 已不存在")

    # ========================================================================
    # S2: 并发 REGISTER 更新 x CALL（写读交错）
    # ========================================================================
    print("\n--- S2: 并发 REGISTER 更新 x CALL ---")

    s2_stop = threading.Event()
    k_caller_threads = 6

    r = function_pool_register("c2_func", s2_v1_func)
    check(r.status == ResStatus.eOk, "S2: 注册 c2_func 为 v1 实现")

    # 启动 caller 线程（持续 CALL 直到 stop）
    callers = []
    for _ in range(k_caller_threads):
        def caller():
            while not s2_stop.is_set():
                resp = function_pool_call("c2_func", None)
                if resp.status == ResStatus.eOk:
                    s2_total.append(1)
        t = threading.Thread(target=caller)
        callers.append(t)
        t.start()

    # 主线程做 50 轮 REGISTER 更新交替 v1/v2
    k_update_rounds = 50
    for round_ in range(k_update_rounds):
        if round_ % 2 == 0:
            function_pool_register("c2_func", s2_v2_func)
        else:
            function_pool_register("c2_func", s2_v1_func)
        # 短暂让步让 caller 线程有机会执行
        time.sleep(0.0001)

    s2_stop.set()
    for t in callers:
        t.join()

    v1 = len(s2_v1)
    v2 = len(s2_v2)
    total = len(s2_total)

    check(v1 + v2 == total,
          f"S2: 写读交错守恒 v1({v1}) + v2({v2}) == total({total})——无撕裂")
    check(v1 > 0, "S2: v1 实现被观测到（更新前后均有调用命中 v1）")
    check(v2 > 0, "S2: v2 实现被观测到（更新确实生效）")
    check(total > 0, "S2: 总调用数 > 0（caller 线程确实执行了）")

    # 清理
    function_pool_unregister("c2_func")
    r = function_pool_call("c2_func", None)
    check(r.status == ResStatus.eNo, "S2: 清理后 c2_func 已不存在")

    # ========================================================================
    # S3: 并发 UNREGISTER x CALL（锁外调用安全性）
    # ========================================================================
    print("\n--- S3: 并发 UNREGISTER x CALL（锁外调用安全性）---")

    r = function_pool_register("c3_gated", s3_gated_func)
    check(r.status == ResStatus.eOk, "S3: 注册 c3_gated（gate 函数：体内等待释放）")

    # 启动 caller 线程：CALL c3_gated → 进入 gate 函数 → 等待释放
    caller = threading.Thread(
        target=lambda: function_pool_call("c3_gated", None))
    caller.start()

    # 等 gate 函数开始执行（证明读锁已释放，函数在锁外执行中）
    started = s3_gate_started.wait(timeout=5)
    check(started,
          "S3: gate 函数已进入执行（callFunction 的读锁已释放，锁外调用中）")

    # 在 gate 函数执行期间 UNREGISTER——写锁获取成功（无读锁持有者）
    r = function_pool_unregister("c3_gated")
    check(r.status == ResStatus.eOk,
          "S3: 在途调用期间 UNREGISTER 成功（写锁无阻塞——读锁已释放）")

    # 释放 gate → 在途调用完成
    s3_gate_release.set()
    caller.join()

    check(len(s3_exec) == 1,
          "S3: 在途调用完成（erase 不影响已取出的函数——锁外调用安全性核心证明）")

    # 注销后新 CALL → eNo
    r = function_pool_call("c3_gated", None)
    check(r.status == ResStatus.eNo, "S3: 注销后新 CALL 返回 eNo（注销生效）")

    # ========================================================================
    # S4: 并发内省 x 变更（快照一致性）
    # ========================================================================
    print("\n--- S4: 并发内省 x 变更（快照一致性）---")

    k_churn_threads = 3
    k_churn_rounds = 60
    k_introspect_threads = 2
    k_introspect_rounds = 30

    snapshots = []  # 快照条目数；GIL 下 append 原子免锁

    def churn_worker(t):
        # churn 线程：并发 REGISTER/UNREGISTER 唯一名
        for round_ in range(k_churn_rounds):
            name = f"c4_t{t}_{round_}"
            function_pool_register(name, s5_probe_func)
            # 短暂让步让内省线程有机会观测
            if round_ % 10 == 9:
                time.sleep(0.00005)
            function_pool_unregister(name)

    def introspect_worker():
        # 内省线程：轮询 INFO_ALL 并记录快照（解包 FuncInfoList 取列表长度）
        for _ in range(k_introspect_rounds):
            resp = function_pool_info_all()
            if resp.status == ResStatus.eOk and isinstance(resp.data, list):
                snapshots.append(len(resp.data))
            time.sleep(0.0002)

    threads = [threading.Thread(target=churn_worker, args=(t,))
               for t in range(k_churn_threads)]
    threads += [threading.Thread(target=introspect_worker)
                for _ in range(k_introspect_threads)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    # 验证快照收集
    snapshot_count = len(snapshots)

    check(snapshot_count > 0,
          f"S4: 收集到 {snapshot_count} 个 INFO_ALL 快照")

    # 清理（churn 线程已自行注销，但确认池干净）
    for t in range(k_churn_threads):
        for round_ in range(k_churn_rounds):
            function_pool_unregister(f"c4_t{t}_{round_}")

    # ========================================================================
    # S5: 全量 churn 混合守恒
    # ========================================================================
    print("\n--- S5: 全量 churn 混合守恒 ---")

    k_rounds = 200
    s5_caller_ok = []
    s5_caller_no = []
    s5_caller_total = []
    s5_unreg_ok = []
    s5_unreg_no = []
    s5_reg_ok = []
    s5_introspect = []

    def registerer():
        # 2 线程：反复注册/更新同名（最大互斥争用）
        for _ in range(k_rounds):
            resp = function_pool_register("c5_probe", s5_probe_func)
            if resp.status == ResStatus.eOk:
                s5_reg_ok.append(1)

    def s5_caller():
        # 2 线程：CALL 同名（可能 eOk 或 eNo 取决于 unregister 时序）
        for _ in range(k_rounds):
            resp = function_pool_call("c5_probe", None)
            s5_caller_total.append(1)
            if resp.status == ResStatus.eOk:
                s5_caller_ok.append(1)
            elif resp.status == ResStatus.eNo:
                s5_caller_no.append(1)

    def unregisterer():
        # 1 线程：反复注销同名
        for _ in range(k_rounds):
            resp = function_pool_unregister("c5_probe")
            if resp.status == ResStatus.eOk:
                s5_unreg_ok.append(1)
            elif resp.status == ResStatus.eNo:
                s5_unreg_no.append(1)
            # 短暂让步让 registerer 有机会重新注册
            time.sleep(0.00005)

    def introspector():
        # 1 线程：INFO_NAMES 轮询
        for _ in range(k_rounds):
            resp = function_pool_info_names()
            if resp.status == ResStatus.eOk:
                s5_introspect.append(1)
            time.sleep(0.0001)

    threads = [threading.Thread(target=registerer) for _ in range(2)]
    threads += [threading.Thread(target=s5_caller) for _ in range(2)]
    threads += [threading.Thread(target=unregisterer),
                threading.Thread(target=introspector)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    caller_ok = len(s5_caller_ok)
    caller_no = len(s5_caller_no)
    caller_total = len(s5_caller_total)
    expected_total = 2 * k_rounds

    check(caller_ok + caller_no == caller_total,
          f"S5: caller 守恒 ok({caller_ok}) + no({caller_no}) == total({caller_total})")
    check(caller_total == expected_total,
          f"S5: caller 总尝试 == {expected_total}")
    check(len(s5_reg_ok) == 2 * k_rounds,
          "S5: registerer 全部成功（注册/更新幂等）")
    check(len(s5_unreg_ok) + len(s5_unreg_no) == k_rounds,
          f"S5: unregisterer 守恒 ok+no == {k_rounds}")
    check(len(s5_introspect) > 0,
          "S5: introspector 成功观测到 INFO_NAMES（无崩溃）")
    check(caller_ok > 0,
          "S5: 至少部分 CALL 成功（registerer 在 unregisterer 间隙注册了函数）")

    # 清理
    function_pool_unregister("c5_probe")

    print(f"\n=== Result: {PASS}/{PASS + FAIL} passed, {FAIL} failed ===")
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()
