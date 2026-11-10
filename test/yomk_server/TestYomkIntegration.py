"""
TestYomkIntegration.py —— 跨模块端到端集成测试（覆盖单模块测试无法
覆盖的跨模块路径与 shutdown 统一收尾）

覆盖内容（3 个 Section，单进程一次 server().async_thread(1).init()，断言以运行为准）：
S3. async 回调内重入：异步 monitor 回调（监控池线程）内同步 ctx().set() 重入
    /YomkContext/set 并触发另一键的同步 monitor——不死锁、异步/同步计数守恒、
    异步按提交序保序（末值可读）
S2. 删除内置服务契约与恢复：srv_name("/YomkLogger").remove() → 服务级 eNo + 精确消息；
    server().srv(YomkLogger).create() 无参重注册（构造函数固定名 "/YomkLogger" 生效）
    → LOGGER API 恢复且全链路可用
S1. shutdown 全链路统一收尾（压轴）：event_loop().post() 积压、to_sink 发射缓冲、
    异步 monitor 投递三类在途状态一次性排空——monitor 计数守恒、文件落盘
    （FileLogger __del__ write，CPython 引用计数确定性保证析构即落盘）、
    API 回归 eInvalid + "YomkServer is not initialized"（门面守卫级）、二次 shutdown 幂等

执行顺序约束：server().init 为进程级一次性（shutdown 后不可再 init），故 shutdown 段
必须压轴，重入/删除恢复段在其前完成

语言机制说明：
- 并发计数用 list.append/len（GIL 单步原子，免锁）；观测回调为模块级函数形态
- 文件落盘验证依赖 CPython 引用计数确定性（shutdown 返回时 FileLogger 引用链已断，
  __del__ 同步触发 write，析构即落盘）

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import os
import shutil
import sys
import tempfile
import time

from yomk.msg.server import ResStatus, Response
from yomk.msg.ctx import Context
from yomk.server import server
from yomk.ctx import ctx
from yomk.event_loop import event_loop
from yomk.srv_info import srv_info
from yomk.log import YomkLogger, log, log_mgr


g_failed = 0

def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1

def raw_ctx(endpoint, pkg):
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息断言经 server 域
    # url/msg/request 裸链直取 Response（端点行为与链式终结一致）
    return server().url("/YomkContext/" + endpoint).msg(pkg).request()

def wait_until(
cond, timeout_ms):
    # 限时轮询：条件满足返回 True，超时返回 False
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        if cond():
            return True
        time.sleep(0.01)
    return cond()

def test_tmp_root():
    # 测试临时根：run_tests.sh 注入（test/test_logs/tmp），独立运行回退系统临时目录
    return os.environ.get("YOMK_TEST_TMP_ROOT") or tempfile.gettempdir()

# ---- S3 观测计数（list.append GIL 原子承载，写于监控池线程、读于收敛等待后） ----
g_reentry_async = []
g_reentry_sync = []

def on_reentry_async(ctx_obj):
    # 异步 monitor：监控池线程内同步重入 /YomkContext/set（无捕获 + append 计数）
    g_reentry_async.append(1)
    ctx().key("reentry_out").value("out_" + str(len(g_reentry_async))).set()

def on_reentry_sync(ctx_obj):
    # 同步 monitor：重入 SET 的内联观察点（每次重入 SET 恰好触发一次）
    g_reentry_sync.append(1)

# S3. 异步回调内重入（async monitor -> 同步 set -> 同步 monitor）
def test_s3_reentry(K):
    print("===== S3. 异步回调内重入（async monitor -> 同步 set -> 同步 monitor） =====")
    del g_reentry_async[:]
    del g_reentry_sync[:]

    check(ctx().monitor().enable() == ResStatus.eOk, "开启 monitor 开关")
    check(ctx().key("reentry_key").value("k0").create() == ResStatus.eOk,
          "创建 reentry_key 成功")
    check(ctx().key("reentry_out").value("out_init").create() == ResStatus.eOk,
          "创建 reentry_out 成功")

    check(ctx().key("reentry_key").monitor_fn(on_reentry_async)
          .async_(True).apply() == ResStatus.eOk,
          "挂异步 monitor（监控池线程内同步重入 /YomkContext/set）")
    check(ctx().key("reentry_out").monitor_fn(on_reentry_sync).apply() == ResStatus.eOk,
          "挂同步 monitor（重入 SET 的内联观察点）")

    all_set_ok = True
    for i in range(K):
        if ctx().key("reentry_key").value("k" + str(i)).set() != ResStatus.eOk:
            all_set_ok = False
    check(all_set_ok, f"K 次投递 SET 全部 eOk（K={K}）")

    check(wait_until(lambda: len(g_reentry_sync) == K, 5000),
          "重入链路在限时内收敛（不死锁，测试自然完成即证）")
    check(len(g_reentry_async) == K, "异步 monitor 计数守恒 == K")
    check(len(g_reentry_sync) == K, "重入 SET 触发同步 monitor 计数守恒 == K")

    last = ctx().key("reentry_out").msg().with_default("").get()
    check(last == "out_" + str(K),
          "重入写入末值可读（异步 monitor 按提交序保序，末次重入为 out_K）")

    cleanup_ok = ctx().monitor().disable() == ResStatus.eOk and \
        ctx().key("reentry_key").destroy() == ResStatus.eOk and \
        ctx().key("reentry_out").destroy() == ResStatus.eOk
    check(cleanup_ok, "S3 清场：OFF_MONITOR 与两键 DESTROY 全部 eOk")

# S2. 删除内置服务契约与恢复（/YomkLogger）
def test_s2_restore():
    print("===== S2. 删除内置服务契约与恢复（/YomkLogger） =====")
    check(server().srv_name("/YomkLogger").remove() == 0, "删除内置 /YomkLogger 返回 0")
    after_del = log_mgr().console().list()
    check(after_del.status == ResStatus.eNo
          and after_del.msg == "service not found: /YomkLogger",
          "删除后 LOGGER API 服务级 eNo + 精确消息")
    check("/YomkLogger" not in (srv_info().services().data or []),
          "服务表快照不再含 /YomkLogger")

    check(server().srv(YomkLogger).create() == 0,
          "new_service 无参重注册返回 0（构造函数固定名生效）")
    restored = log_mgr().console().list()
    check(restored.status == ResStatus.eOk and "main" in (restored.data or []),
          "重注册后 LOGGER API 恢复且含 main")
    check(log().tag("main").info("[integration]", "s2 relink marker") == ResStatus.eOk,
          "重注册后日志 API 全链路可用（console_log eOk）")

# ---- S1 观测计数 ----
g_drain_calls = []

def on_drain(ctx_obj):
    g_drain_calls.append(1)

# S1. shutdown 全链路统一收尾（压轴）
def test_s1_shutdown(N):
    print("===== S1. shutdown 全链路统一收尾（压轴） =====")
    tmp_dir = os.path.join(test_tmp_root(), "yomk_integration_s1_" + str(os.getpid()))
    try:
        os.makedirs(tmp_dir, exist_ok=True)
        check(True, "S1 临时目录创建成功: " + tmp_dir)
    except OSError as e:
        check(False, "S1 临时目录创建失败: " + str(e))
        return

    # 事件积压（不等待执行完成；积压处理语义不保证全执行，仅验证 shutdown 有限时间返回）
    check(event_loop().loop("integration_loop").start() == ResStatus.eOk,
          "启动 integration_loop")
    post_ok = True
    for _ in range(100):
        if event_loop().loop("integration_loop").handle_fn(lambda r: Response(ResStatus.eOk)).post() != ResStatus.eOk:
            post_ok = False
    check(post_ok, "100 个积压事件全部 POST eOk")

    # 文件日志：to_sink 发射仅入缓冲，shutdown 链 FileLogger 释放 __del__ write 落盘
    check(log_mgr().dir(tmp_dir).file("integration_file").create() == ResStatus.eOk,
          "file_log_create 返回 eOk")
    check(log().to_sink("integration_file").info("s1_shutdown_flush_marker") == ResStatus.eOk,
          "file_log_info 入缓冲返回 eOk（日志器名与 CREATE 一致）")

    # 异步 monitor 投递（shutdown 排空断言主体；S3 清场时开关已关，此处重新开启）
    check(ctx().monitor().enable() == ResStatus.eOk, "重新开启 monitor 开关")
    check(ctx().key("drain_key").value("d0").create() == ResStatus.eOk, "创建 drain_key 成功")
    del g_drain_calls[:]
    check(ctx().key("drain_key").monitor_fn(on_drain)
          .async_(True).apply() == ResStatus.eOk,
          "挂 drain_key 异步 monitor")
    drain_set_ok = True
    for i in range(N):
        if ctx().key("drain_key").value("d" + str(i)).set() != ResStatus.eOk:
            drain_set_ok = False
    check(drain_set_ok, f"N 次异步通知投递全部 eOk（N={N}）")

    # 统一收尾：排空异步 monitor + 落盘文件日志 + 清空服务表
    server().shutdown()

    check(len(g_drain_calls) == N,
          "shutdown 排空后异步 monitor 计数 == 投递次数（无迟到无丢失）")

    log_path = os.path.join(tmp_dir, "integration_file.log")
    content = ""
    if os.path.exists(log_path):
        with open(log_path, "r") as f:
            content = f.read()
    check("s1_shutdown_flush_marker" in content,
          "shutdown 返回后文件日志已落盘且含标记串（__del__ write）")

    # API 守卫级回归：g_server 已置空 → eInvalid + "YomkServer is not initialized"
    post_loop = event_loop().loop("integration_loop").post()
    check(post_loop == ResStatus.eInvalid,
          "shutdown 后 event_loop_post 回归 eInvalid（写路径收敛为 ResStatus）")
    check(event_loop().list().msg == "YomkServer is not initialized",
          "shutdown 后 event_loop_post 守卫契约消息同根（读路径 INFO_LOOPS）")
    post_ctx = ctx().key("drain_key").value("x").set()
    check(post_ctx == ResStatus.eInvalid,
          "shutdown 后 context_set 回归 eInvalid（写路径收敛为 ResStatus）")
    check(raw_ctx("set", Context("drain_key", "x", "main")).msg
          == "YomkServer is not initialized",
          "shutdown 后 context_set 守卫契约消息同根（server 域裸链）")
    post_log = log_mgr().all()
    check(post_log.status == ResStatus.eInvalid
          and post_log.msg == "YomkServer is not initialized",
          "shutdown 后 logger_info_all 回归 eInvalid + 守卫消息")
    post_info = srv_info().all()
    check(post_info.status == ResStatus.eInvalid
          and post_info.msg == "YomkServer is not initialized",
          "shutdown 后 server_info_all 回归 eInvalid + 守卫消息")

    server().shutdown()  # 幂等：二次调用空转安全
    check(True, "shutdown 二次调用幂等无崩溃")

    shutil.rmtree(tmp_dir, ignore_errors=True)

def main():
    server().async_thread(1).init()

    K = 20
    test_s3_reentry(K)
    test_s2_restore()
    test_s1_shutdown(50)

    if g_failed == 0:
        print("TestYomkIntegration all check passed. (0 failed)")
        return 0
    print(f"TestYomkIntegration FAILED ({g_failed} failed)")
    return 1

if __name__ == "__main__":
    sys.exit(main())
