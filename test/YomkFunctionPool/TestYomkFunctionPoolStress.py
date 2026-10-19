"""
TestYomkFunctionPoolStress.py —— YomkFunctionPool 压力测试（5 节 17 断言 + 7 [BASELINE]）

节结构逐节对齐同名 C++ 测试（S1 单线程 register/call 热路径/unregister 吞吐 /
S2 多线程并发 call 吞吐 / S3 全生命周期 churn 四相守恒 / S4 超大函数名压力 /
S5 API 端到端混合负载五相守恒），零剔除。

语言映射：
- YOMK_FUNCTIONPOOL_* 宏 → 门面 function_pool_* 六 API（覆盖 invoke 路由链）
- std::atomic 计数 → list.append（GIL 原子，len() 读数）
- std::chrono::steady_clock → time.perf_counter()
- recordBaseline 格式逐字对齐（ops/s 记录，不断言 timing，仅断言守恒与正确性）
- 规模经 YOMK_TEST_STRESS_SCALE 环境变量参数化（缺省 100000，clamp 下限 1000），
  与 run_tests.sh 快速模式（10000）/--full（缺省 10 万级）三档惯例一致
"""

import os
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


def stress_scale():
    """YOMK_TEST_STRESS_SCALE 参数化（缺省 100000，clamp 下限 1000）。"""
    env = os.environ.get("YOMK_TEST_STRESS_SCALE")
    n = int(env) if env else 100000
    return n if n >= 1000 else 1000


def record_baseline(section, ops, elapsed_ms):
    """[BASELINE] 吞吐记录（不断言 timing，环境相关）。"""
    ops_per_sec = (ops / (elapsed_ms / 1000.0)) if elapsed_ms > 0.0 else 0.0
    print(
        f"[BASELINE] {section}: {ops_per_sec:.0f} ops/s ({ops} ops in {elapsed_ms:.1f} ms)")


# ============================================================================
# 测试用函数
# ============================================================================

def stress_noop_func(pkg):
    return YomkResponse(ResStatus.eOk, "ok")


s2_exec = []  # std::atomic 计数映射：list.append GIL 原子


def stress_count_func(pkg):
    s2_exec.append(1)
    return YomkResponse(ResStatus.eOk, "ok")


def make_name(prefix, index):
    """生成唯一函数名（std::to_string 同构）。"""
    return f"{prefix}{index}"


def main():
    global PASS, FAIL
    print("=== TestYomkFunctionPoolStress ===")

    n = stress_scale()
    print(f"[CONFIG] YOMK_TEST_STRESS_SCALE = {n}")

    server = init(1)
    check(server is not None, "init(1) 返回 server 实例")

    # ========================================================================
    # S1: 单线程 register/call/unregister 吞吐
    # ========================================================================
    print("\n--- S1: 单线程 register/call/unregister 吞吐 ---")

    # Phase A: register N unique names
    reg_ok = 0
    t0 = time.perf_counter()
    for i in range(n):
        resp = function_pool_register(
            make_name("s1_func_", i), stress_noop_func)
        if resp.status == ResStatus.eOk:
            reg_ok += 1
    t1 = time.perf_counter()
    reg_ms = (t1 - t0) * 1000.0
    record_baseline("S1_register", n, reg_ms)

    check(reg_ok == n, f"S1: register 守恒 {reg_ok}/{n}")

    # Phase B: call one pre-registered hot function N times
    call_ok = 0
    t2 = time.perf_counter()
    for _ in range(n):
        resp = function_pool_call("s1_func_0", None)
        if resp.status == ResStatus.eOk:
            call_ok += 1
    t3 = time.perf_counter()
    call_ms = (t3 - t2) * 1000.0
    record_baseline("S1_call_hotpath", n, call_ms)

    check(call_ok == n, f"S1: call 热路径守恒 {call_ok}/{n}")

    # Phase C: unregister N unique names
    unreg_ok = 0
    t4 = time.perf_counter()
    for i in range(n):
        resp = function_pool_unregister(make_name("s1_func_", i))
        if resp.status == ResStatus.eOk:
            unreg_ok += 1
    t5 = time.perf_counter()
    unreg_ms = (t5 - t4) * 1000.0
    record_baseline("S1_unregister", n, unreg_ms)

    check(unreg_ok == n, f"S1: unregister 守恒 {unreg_ok}/{n}")

    # ========================================================================
    # S2: 多线程并发 call 吞吐
    # ========================================================================
    print("\n--- S2: 多线程并发 call 吞吐 ---")

    k_threads = 8
    per_thread = n // k_threads
    total_calls = per_thread * k_threads

    del s2_exec[:]
    function_pool_register("s2_hot", stress_count_func)

    workers = []
    t0 = time.perf_counter()
    for _ in range(k_threads):
        def worker():
            for _ in range(per_thread):
                function_pool_call("s2_hot", None)
        t = threading.Thread(target=worker)
        workers.append(t)
        t.start()
    for t in workers:
        t.join()
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    record_baseline("S2_concurrent_call", total_calls, elapsed)

    check(len(s2_exec) == total_calls,
          f"S2: 并发 call 守恒 {len(s2_exec)}/{total_calls}")

    function_pool_unregister("s2_hot")

    # ========================================================================
    # S3: 全生命周期 churn（register→call→unregister→call(eNo) x N）
    # ========================================================================
    print("\n--- S3: 全生命周期 churn ---")

    reg_ok = 0
    call_ok = 0
    unreg_ok = 0
    post_no = 0

    t0 = time.perf_counter()
    for i in range(n):
        name = make_name("s3_churn_", i)
        if function_pool_register(name, stress_noop_func).status == ResStatus.eOk:
            reg_ok += 1
        if function_pool_call(name, None).status == ResStatus.eOk:
            call_ok += 1
        if function_pool_unregister(name).status == ResStatus.eOk:
            unreg_ok += 1
        if function_pool_call(name, None).status == ResStatus.eNo:
            post_no += 1
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    record_baseline("S3_churn_4ops", n * 4, elapsed)

    check(reg_ok == n, f"S3: churn register 守恒 {reg_ok}/{n}")
    check(call_ok == n, f"S3: churn call 守恒 {call_ok}/{n}")
    check(unreg_ok == n, f"S3: churn unregister 守恒 {unreg_ok}/{n}")
    check(post_no == n, f"S3: churn post-unregister eNo 守恒 {post_no}/{n}")

    # ========================================================================
    # S4: 超大函数名压力（65536 字节）
    # ========================================================================
    print("\n--- S4: 超大函数名压力 ---")

    rounds = n // 100  # 降低轮次避免内存压力
    big_name = "X" * 65536

    reg_ok = 0
    call_ok = 0
    unreg_ok = 0

    t0 = time.perf_counter()
    for _ in range(rounds):
        if function_pool_register(big_name, stress_noop_func).status == ResStatus.eOk:
            reg_ok += 1
        if function_pool_call(big_name, None).status == ResStatus.eOk:
            call_ok += 1
        if function_pool_unregister(big_name).status == ResStatus.eOk:
            unreg_ok += 1
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    record_baseline("S4_bigname_3ops", rounds * 3, elapsed)

    check(reg_ok == rounds, f"S4: 大名 register 守恒 {reg_ok}/{rounds}")
    check(call_ok == rounds, f"S4: 大名 call 守恒 {call_ok}/{rounds}")
    check(unreg_ok == rounds, f"S4: 大名 unregister 守恒 {unreg_ok}/{rounds}")

    # 最终确认已清理
    r = function_pool_call(big_name, None)
    check(r.status == ResStatus.eNo, "S4: 大名最终已注销（eNo）")

    # ========================================================================
    # S5: API 端到端混合负载
    # ========================================================================
    print("\n--- S5: API 端到端混合负载 ---")

    k_group_size = 10
    groups = n // k_group_size

    reg_ok = 0
    call_ok = 0
    info_names_ok = 0
    info_all_ok = 0
    unreg_ok = 0

    t0 = time.perf_counter()
    for g in range(groups):
        # register 10
        for k in range(k_group_size):
            resp = function_pool_register(make_name("s5_mix_", g * k_group_size + k),
                                          stress_noop_func)
            if resp.status == ResStatus.eOk:
                reg_ok += 1
        # call 10
        for k in range(k_group_size):
            resp = function_pool_call(
                make_name("s5_mix_", g * k_group_size + k), None)
            if resp.status == ResStatus.eOk:
                call_ok += 1
        # introspect
        if function_pool_info_names().status == ResStatus.eOk:
            info_names_ok += 1
        if function_pool_info_all().status == ResStatus.eOk:
            info_all_ok += 1
        # unregister 10
        for k in range(k_group_size):
            resp = function_pool_unregister(
                make_name("s5_mix_", g * k_group_size + k))
            if resp.status == ResStatus.eOk:
                unreg_ok += 1
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    # 每组 10+10+1+1+10 = 32 ops
    record_baseline("S5_mixed_32ops_per_group", groups * 32, elapsed)

    expected_per_type = groups * k_group_size
    check(reg_ok == expected_per_type,
          f"S5: 混合 register 守恒 {reg_ok}/{expected_per_type}")
    check(call_ok == expected_per_type,
          f"S5: 混合 call 守恒 {call_ok}/{expected_per_type}")
    check(info_names_ok == groups,
          f"S5: INFO_NAMES 守恒 {info_names_ok}/{groups}")
    check(info_all_ok == groups,
          f"S5: INFO_ALL 守恒 {info_all_ok}/{groups}")
    check(unreg_ok == expected_per_type,
          f"S5: 混合 unregister 守恒 {unreg_ok}/{expected_per_type}")

    print(f"\n=== Result: {PASS}/{PASS + FAIL} passed, {FAIL} failed ===")
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()
