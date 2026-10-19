"""
TestYomkContextCheckerMonitor.py —— YomkContext checker 与 monitor（同步/异步）白盒测试（Context 模块 MC2）

对齐 main Test/YomkContext/TestYomkContextCheckerMonitor.cpp，覆盖内容：
1. set_checker 边界：空 key、None checkFunc、不存在 key、正常设置
2. turn_on_checker/turn_off_checker 开关语义
3. checker accept/reject：accept 放行、reject 拦截且 value 不变
4. 空 checker 守卫（对应前置修复）：全局开启 checker 后对未设 checker 的 key set 视为 accept，不崩溃
5. checker 关闭后不生效
6. set_monitor 边界：空 key、None func、不存在 key、正常设置
7. turn_on_monitor/turn_off_monitor 开关语义
8. 同步 monitor 触发（拿到最新数据）、关闭后不触发
9. 同步 monitor 异常吞掉（异常后后续 monitor 继续执行）
10. 多同步 monitor 按注册顺序触发
11. 异步 monitor 触发与顺序（单线程池保序）
12. 并发 set 异步保序：锁内入队 ⇒ 末条异步通知收敛到最终值（C1 回归）+ 每线程子序列严格递增（python 附加强化）
13. 异步 monitor 排空（deinit）：shutdown 前排空全部异步任务

剔除与差异说明（语言机制映射，非功能缺口）：
- main 第 9 节"抛非 std::exception（throw 42）"变体：main 以 catch(...) 兜一切异常，
  python 服务端以 except Exception 兜底（覆盖 python 惯例全部异常类型），二者语义等价；
  python 侧以第二种 Exception 子类承载双异常形态。KeyboardInterrupt/SystemExit 等
  BaseException 按 python 惯例不被 except Exception 捕获（与 main catch(...) 的有意
  差异：保持 Ctrl-C 中断语义），不构造该病态用例
- main 的异步 monitor 单函数 + /*async=*/true 标志 -> python 经门面
  context_set_monitor(key, func, async_monitor=True) 同语义映射（门面把用户回调
  投递到异步位、同步位以 no-op 占位，见 YomkApi.context_set_monitor）

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
全程使用门面单例拉起内置 /YomkContext；异步排空用例置于末尾并调用 shutdown
（python 门面 shutdown 后 g_server 置 None、init 不再重建，与 main call_once 约束同构）；
服务删除后重新注册/池重建归 MC3
"""

import sys
import time

from YomkApi import (
    context_create,
    context_get,
    context_set,
    context_set_checker,
    context_set_monitor,
    context_turn_off_checker,
    context_turn_off_monitor,
    context_turn_on_checker,
    context_turn_on_monitor,
    init,
    shutdown,
)
from YomkServerPy import CheckStatus, ResStatus

g_failed = 0


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def wait_for_count(bucket, target, timeout_s):
    # 10ms 步进轮询等待计数达到 target（对齐 main waitForCount）
    for _ in range(int(timeout_s / 0.01)):
        if len(bucket) >= target:
            return True
        time.sleep(0.01)
    return len(bucket) >= target


# ---- 文件级观测变量（每节首重置；计数用 list.append 原子承载，读取用 len） ----
g_checker_calls = []       # checker 被调用计数
g_sync_monitor_calls = []  # 同步 monitor 被调用计数
g_throw_a_calls = []       # 抛 RuntimeError 的 monitor 调用计数
g_throw_b_calls = []       # 抛自定义异常的 monitor 调用计数
g_async_calls = []         # 异步 monitor（顺序/并发用例）调用计数
g_drain_calls = []         # 异步 monitor（排空用例）调用计数
g_last_sync_value = [""]   # 最近一次同步 monitor 看到的 value（单元素容器）
g_order = []               # 多同步 monitor 触发顺序（单次 set 内串行 append）
g_async_order = []         # 异步 monitor 到达顺序（单线程池 worker 内 append，序即执行序）


def reset_globals():
    g_checker_calls.clear()
    g_sync_monitor_calls.clear()
    g_throw_a_calls.clear()
    g_throw_b_calls.clear()
    g_async_calls.clear()
    g_drain_calls.clear()
    g_last_sync_value[0] = ""
    g_order.clear()
    g_async_order.clear()


def main():
    reset_globals()

    srv = init(1)
    check(srv is not None, "init 返回非空服务器")

    # ============ Section 1: set_checker 边界 ============
    resp = context_create("ck_key", "init")
    check(resp.status == ResStatus.eOk, "创建 ck_key 成功")

    resp = context_set_checker("", lambda c: CheckStatus.eAccept)
    check(resp.status == ResStatus.eNo, "set_checker 空 key 返回 eNo")
    check(resp.msg == "key is empty", "set_checker 空 key 消息一致")

    resp = context_set_checker("ck_key", None)
    check(resp.status == ResStatus.eNo, "set_checker None checkFunc 返回 eNo")
    check(resp.msg == "checkFunc is empty", "set_checker None checkFunc 消息一致")

    resp = context_set_checker("no_such_key", lambda c: CheckStatus.eAccept)
    check(resp.status == ResStatus.eNo, "set_checker 不存在 key 返回 eNo")
    check(resp.msg == "key is not exist", "set_checker 不存在 key 消息一致")

    resp = context_set_checker("ck_key", lambda c: CheckStatus.eAccept)
    check(resp.status == ResStatus.eOk, "set_checker 正常设置返回 eOk")
    check(resp.msg == "set checker success", "set_checker 成功消息一致")

    # ============ Section 2: checker 开关 ============
    resp = context_turn_on_checker()
    check(resp.status == ResStatus.eOk, "turn_on_checker 返回 eOk")
    check(resp.msg == "turn on checker success", "turn_on_checker 消息一致")

    resp = context_turn_off_checker()
    check(resp.status == ResStatus.eOk, "turn_off_checker 返回 eOk")
    check(resp.msg == "turn off checker success", "turn_off_checker 消息一致")

    # ============ Section 3: checker accept/reject ============
    # accept-checker，全局开启
    context_set_checker(
        "ck_key",
        lambda c: (g_checker_calls.append(1), CheckStatus.eAccept)[1])
    context_turn_on_checker()

    g_checker_calls.clear()
    resp = context_set("ck_key", "accepted")
    check(resp.status == ResStatus.eOk, "accept-checker 下 set 返回 eOk")
    check(len(g_checker_calls) == 1, "accept-checker 被调用 1 次")

    # reject-checker 覆盖
    context_set_checker(
        "ck_key",
        lambda c: (g_checker_calls.append(1), CheckStatus.eReject)[1])
    g_checker_calls.clear()
    resp = context_set("ck_key", "rejected")
    check(resp.status == ResStatus.eNo, "reject-checker 下 set 返回 eNo")
    check(resp.msg == "checker reject set context", "reject-checker 消息一致")
    check(len(g_checker_calls) == 1, "reject-checker 被调用 1 次")

    # value 未变（仍为 accepted）
    check(context_get("ck_key", "default") == "accepted",
          "reject 后 value 保持不变")

    # ============ Section 4: 空 checker 守卫（对应前置修复）============
    # checker 仍全局开启；ck_nochecker 从未设置 checker
    resp = context_create("ck_nochecker", "v0")
    check(resp.status == ResStatus.eOk, "创建 ck_nochecker 成功")

    resp = context_set("ck_nochecker", "v1")
    check(resp.status == ResStatus.eOk,
          "全局开启 checker 但未设 checker 的 key set 视为 accept（不崩溃）")

    check(context_get("ck_nochecker", "default") == "v1",
          "空 checker 守卫下 value 正常更新")

    # ============ Section 5: checker 关闭后不生效 ============
    context_turn_off_checker()
    # ck_key 当前为 reject-checker，但 checker 已关闭，set 应成功
    resp = context_set("ck_key", "after_off")
    check(resp.status == ResStatus.eOk,
          "checker 关闭后 reject-checker 不生效，set 成功")

    check(context_get("ck_key", "default") == "after_off",
          "checker 关闭后 value 更新成功")

    # ============ Section 6: set_monitor 边界 ============
    resp = context_create("mon_bound", "m0")
    check(resp.status == ResStatus.eOk, "创建 mon_bound 成功")

    resp = context_set_monitor("", lambda c: None)
    check(resp.status == ResStatus.eNo, "set_monitor 空 key 返回 eNo")
    check(resp.msg == "key is empty", "set_monitor 空 key 消息一致")

    resp = context_set_monitor("mon_bound", None)
    check(resp.status == ResStatus.eNo, "set_monitor None func 返回 eNo")
    check(resp.msg == "context monitor function is empty",
          "set_monitor None func 消息一致")

    resp = context_set_monitor("no_such_key", lambda c: None)
    check(resp.status == ResStatus.eNo, "set_monitor 不存在 key 返回 eNo")
    check(resp.msg == "key is not exist", "set_monitor 不存在 key 消息一致")

    resp = context_set_monitor("mon_bound", lambda c: None)
    check(resp.status == ResStatus.eOk, "set_monitor 正常设置返回 eOk")
    check(resp.msg == "set context monitor success", "set_monitor 成功消息一致")

    # ============ Section 7: monitor 开关 ============
    resp = context_turn_on_monitor()
    check(resp.status == ResStatus.eOk, "turn_on_monitor 返回 eOk")
    check(resp.msg == "turn on monitor success", "turn_on_monitor 消息一致")

    resp = context_turn_off_monitor()
    check(resp.status == ResStatus.eOk, "turn_off_monitor 返回 eOk")
    check(resp.msg == "turn off monitor success", "turn_off_monitor 消息一致")

    # ============ Section 8: 同步 monitor 触发 ============
    resp = context_create("mon_sync", "s0")
    check(resp.status == ResStatus.eOk, "创建 mon_sync 成功")

    def on_sync(ctx):
        g_sync_monitor_calls.append(1)
        g_last_sync_value[0] = ctx.value

    context_set_monitor("mon_sync", on_sync)

    context_turn_on_monitor()
    g_sync_monitor_calls.clear()
    g_last_sync_value[0] = ""

    resp = context_set("mon_sync", "s1")
    check(resp.status == ResStatus.eOk, "开启 monitor 后 set 返回 eOk")
    check(len(g_sync_monitor_calls) == 1, "同步 monitor 被调用 1 次")
    check(g_last_sync_value[0] == "s1", "同步 monitor 拿到最新 value s1")

    context_turn_off_monitor()
    g_sync_monitor_calls.clear()
    resp = context_set("mon_sync", "s2")
    check(resp.status == ResStatus.eOk, "关闭 monitor 后 set 仍返回 eOk")
    check(len(g_sync_monitor_calls) == 0, "关闭 monitor 后同步 monitor 不触发")

    # ============ Section 9: 同步 monitor 异常吞掉 ============
    # （main 的非 std::exception 变体映射说明见文件头剔除说明）
    context_turn_on_monitor()
    resp = context_create("mon_throw", "t0")
    check(resp.status == ResStatus.eOk, "创建 mon_throw 成功")

    # monitor A：抛 RuntimeError（对齐 main 抛 std::exception）
    context_set_monitor(
        "mon_throw",
        lambda c: (g_throw_a_calls.append(1),
                   (_ for _ in ()).throw(RuntimeError("sync monitor exception")))[0])
    # monitor B：抛自定义 Exception 子类（对齐 main 抛非 std::exception 变体的双异常形态）
    context_set_monitor(
        "mon_throw",
        lambda c: (g_throw_b_calls.append(1),
                   (_ for _ in ()).throw(ValueError("sync monitor custom exception")))[0])
    # monitor C：正常，验证异常不影响后续 monitor
    context_set_monitor("mon_throw", lambda c: g_sync_monitor_calls.append(1))

    g_throw_a_calls.clear()
    g_throw_b_calls.clear()
    g_sync_monitor_calls.clear()

    resp = context_set("mon_throw", "t1")
    check(resp.status == ResStatus.eOk, "同步 monitor 抛异常被吞掉，set 仍返回 eOk")
    check(len(g_throw_a_calls) == 1, "抛 RuntimeError 的 monitor 被调用")
    check(len(g_throw_b_calls) == 1, "抛自定义异常的 monitor 被调用")
    check(len(g_sync_monitor_calls) == 1, "异常 monitor 之后的正常 monitor 继续执行")
    context_turn_off_monitor()

    # ============ Section 10: 多同步 monitor 顺序 ============
    context_turn_on_monitor()
    resp = context_create("mon_multi", "u0")
    check(resp.status == ResStatus.eOk, "创建 mon_multi 成功")

    g_order.clear()
    context_set_monitor("mon_multi", lambda c: g_order.append("1"))
    context_set_monitor("mon_multi", lambda c: g_order.append("2"))
    context_set_monitor("mon_multi", lambda c: g_order.append("3"))

    resp = context_set("mon_multi", "u1")
    check(resp.status == ResStatus.eOk, "多 monitor set 返回 eOk")
    check(len(g_order) == 3, "3 个同步 monitor 全部触发")
    check(g_order == ["1", "2", "3"], "多同步 monitor 按注册顺序触发")
    context_turn_off_monitor()

    # ============ Section 11: 异步 monitor 触发与顺序 ============
    context_turn_on_monitor()
    resp = context_create("mon_async", "a0")
    check(resp.status == ResStatus.eOk, "创建 mon_async 成功")

    g_async_calls.clear()
    g_async_order.clear()

    def on_async(ctx):
        g_async_order.append(ctx.value)
        g_async_calls.append(1)

    context_set_monitor("mon_async", on_async, True)

    context_set("mon_async", "a1")
    context_set("mon_async", "a2")
    context_set("mon_async", "a3")

    reached = wait_for_count(g_async_calls, 3, 2.0)
    check(reached, "异步 monitor 在等待窗口内全部触发（计数 == 3）")
    check(g_async_order == ["a1", "a2", "a3"],
          "异步 monitor 按 set 顺序到达（单线程池保序）")
    context_turn_off_monitor()

    # ============ Section 12: 并发 set 异步保序（末条收敛真值，C1 回归）============
    context_turn_on_monitor()
    resp = context_create("mon_conc", "c_init")
    check(resp.status == ResStatus.eOk, "创建 mon_conc 成功")

    g_async_calls.clear()
    g_async_order.clear()

    def on_async_conc(ctx):
        g_async_order.append(ctx.value)
        g_async_calls.append(1)

    context_set_monitor("mon_conc", on_async_conc, True)

    # N 个线程并发 set（各值可辨识）；C1 下异步入队在写锁内完成 ⇒ 入队序=提交序，
    # 单线程 FIFO 池 ⇒ 末条异步通知 = 最后一次提交的值 = 最终 CONTEXT_GET 值；
    # 且每线程子序列严格递增（python 附加强化断言，锁内入队 + FIFO 的确定性推论）
    import threading

    k_threads = 8
    k_iters = 20
    workers = []

    def conc_worker(t):
        for i in range(k_iters):
            context_set("mon_conc", f"t{t}_i{i}")

    for t in range(k_threads):
        workers.append(threading.Thread(target=conc_worker, args=(t,)))
    for w in workers:
        w.start()
    for w in workers:
        w.join()

    k_total = k_threads * k_iters
    reached = wait_for_count(g_async_calls, k_total, 15.0)
    check(reached, "并发 set 后异步 monitor 在等待窗口内全部触发（计数 == 总提交数）")

    # 所有写线程已 join，终值稳定
    final_val = context_get("mon_conc", "default")
    check(final_val != "default" and final_val != "", "读取 mon_conc 终值成功")

    check(len(g_async_order) == k_total, "异步顺序记录条数 == 总提交数")
    check(len(g_async_order) > 0 and g_async_order[-1] == final_val,
          "并发 set 下异步末条 == 最终值（锁内入队 ⇒ 末条收敛真值）")

    # 每线程子序列严格递增且恰好完整（确定性断言：入队序=提交序 + 单线程 FIFO）
    per_thread = {t: [] for t in range(k_threads)}
    for v in g_async_order:
        body = v.split("_")
        per_thread[int(body[0][1:])].append(int(body[1][1:]))
    seq_ok = all(per_thread[t] == list(range(k_iters))
                 for t in range(k_threads))
    check(seq_ok, "每线程子序列严格递增且完整（入队序=提交序 + 单线程 FIFO）")
    context_turn_off_monitor()

    # ============ Section 13: 异步 monitor 排空（deinit），末尾调用 shutdown ============
    context_turn_on_monitor()
    resp = context_create("mon_drain", "d0")
    check(resp.status == ResStatus.eOk, "创建 mon_drain 成功")

    g_drain_calls.clear()
    context_set_monitor("mon_drain", lambda c: g_drain_calls.append(1), True)

    n = 50
    for i in range(n):
        context_set("mon_drain", f"d{i}")

    # 立即 shutdown，触发 Context deinit 排空 monitor 池
    shutdown()

    # shutdown 返回后排空已完成，计数应等于投递次数
    check(len(g_drain_calls) == n,
          "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到、无丢失）")

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
