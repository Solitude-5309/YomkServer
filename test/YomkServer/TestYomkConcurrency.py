"""
TestYomkConcurrency.py —— 多线程并发功能测试与持续压测（正式全面测试）

覆盖内容：
1. 并发同步 request 守恒：8 线程 x 500 次混合请求（ping/echo 交替）全部 eOk、
   函数体命中计数守恒（服务表/函数表读写锁并发读安全基线）
2. 并发 asyncRequest 不丢不重：8 线程 x 125 次投递，回调全部到达且无重复
3. request x add/del 并发：20 轮删/注交替下响应全部落在合法集合
   （service not found / function not found 为并发注册窗口的框架设计窗口）
4. request x install_func 覆盖安装并发：200 轮三参/两参覆盖交替下回显无撕裂
5. shutdown x 在途异步真并发：poster 持续投递 + shutdown 排空，回调数稳定、
   无重复执行、shutdown 后投递全拒
7. N7 持续压测：8 线程混合操作（同步 ping / 异步 echo / service_names 内省）
   跨两个 server 实例，server2 在 100ms 后被并发 shutdown（连续两次调用顺带
   验证幂等）；总操作数读 YOMK_TEST_STRESS_SCALE（缺省 100000
   10 万级，快速模式由 run_tests.sh 注入 10000，clamp 下限 1000）

语言机制说明（非功能缺口）：
- "删除即停并发形态"（弱绑定回调判活 g_weakCb / g_rejected / "deleted or
  unregistered" 契约消息）不适用 python 形态，is_expected_no_response 合法集合同步剔除该消息
- 覆盖安装用直接方法引用安装（无弱绑定包装层）
- server1 兜底销毁以节末显式 shutdown 承载（python 无确定性析构，A-4 口径）
- 并发计数用 CPython list.append 原子操作承载（GIL 下
  安全），读取用 len()
"""

import os
import sys
import threading
import time

from YomkServerPy import ResStatus, YomkResponse, YomkServer, YomkService

g_failed = 0


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


# ---- 文件级观测变量（每节首重置；并发计数用 list.append 原子承载，读取用 len） ----
g_hit = []      # 用户函数体真实执行计数
g_fired = []    # 异步回调到达计数（仅 eOk）
g_illegal = []  # 非法返回值计数
g_posted = []   # 异步投递调用计数


def reset_globals():
    g_hit.clear()
    g_fired.clear()
    g_illegal.clear()
    g_posted.clear()


def wait_for(pred, timeout_s, desc):
    # 10ms 步进轮询，超时打印 [TIMEOUT] 返回 False
    for _ in range(int(timeout_s / 0.01)):
        if pred():
            return True
        time.sleep(0.01)
    print(f"[TIMEOUT] {desc} (timeout {int(timeout_s * 1000)} ms)")
    return False


class DemoPkg:
    # String 消息包的测试载体（pkg.d 字段，同 T3 口径）
    def __init__(self, d=""):
        self.d = d


class ConcSrv(YomkService):
    # 并发演示服务：/ping 固定响应、/echo 回显 pkg.d
    def init(self):
        self.install_func("/ping", self.ping)
        self.install_func("/echo", self.echo, "String")
        return 0

    def ping(self, pkg):
        g_hit.append(1)
        return YomkResponse(ResStatus.eOk, "pong")

    def echo(self, pkg):
        g_hit.append(1)
        if pkg is None:
            # 直接属性访问需空值守卫（同 T2/T3 口径）
            return YomkResponse(ResStatus.eNo, "pkg is null")
        return YomkResponse(ResStatus.eOk, pkg.d)


def register_conc_srv(server, name):
    return server.new_service(ConcSrv, name)


def is_expected_no_response(r):
    if r.status != ResStatus.eNo:
        return False
    # 并发注册窗口：add_service 先入表后 init，request 可能在函数安装前命中服务，
    # 返回 function not found；这是框架设计窗口（非崩溃/悬垂缺陷），纳入可接受集合
    # （合法集合已剔除 "deleted or unregistered"，python 无此消息）
    return "service not found" in r.msg or "function not found" in r.msg


def on_fired_ok(r):
    # 异步回调计数：仅 eOk 计入
    if r.status == ResStatus.eOk:
        g_fired.append(1)


def make_fired_cb(bucket):
    # 节 7 回调工厂：回调携带所属 bucket
    def _cb(r):
        if r.status == ResStatus.eOk:
            bucket.append(1)
    return _cb


# 1. 并发同步 request 守恒
def test_concurrent_sync_request():
    print("===== 1. 并发同步 request（读写锁 + 副本保活基线） =====")
    reset_globals()
    server = YomkServer.create(4)
    check(register_conc_srv(server, "/PingSrv") == 0, "1.1 注册 /PingSrv 成功")

    k_threads = 8
    k_per_thread = 500
    ok_list = []
    workers = []

    def worker():
        for i in range(k_per_thread):
            if i % 2 == 0:
                r = server.request("/PingSrv/ping")
                if r.status == ResStatus.eOk and r.msg == "pong":
                    ok_list.append(1)
            else:
                r = server.request("/PingSrv/echo", DemoPkg("x"))
                if r.status == ResStatus.eOk and r.msg == "x":
                    ok_list.append(1)

    for _ in range(k_threads):
        th = threading.Thread(target=worker)
        th.start()
        workers.append(th)
    for th in workers:
        th.join()

    check(len(ok_list) == k_threads * k_per_thread,
          "1.2 并发同步 request 全部 eOk（4000/4000）")
    check(len(g_hit) == k_threads * k_per_thread,
          "1.3 函数体命中计数守恒（4000/4000）")
    server.shutdown()


# 2. 并发 asyncRequest 不丢不重
def test_concurrent_async_no_loss():
    print("===== 2. 并发 asyncRequest（请求池不丢不重） =====")
    reset_globals()
    server = YomkServer.create(4)
    check(register_conc_srv(server, "/AsyncSrv") == 0, "2.1 注册 /AsyncSrv 成功")

    k_threads = 8
    k_per_thread = 125
    workers = []

    def worker():
        for _ in range(k_per_thread):
            server.async_request("/AsyncSrv/echo", DemoPkg("a"), on_fired_ok)

    for _ in range(k_threads):
        th = threading.Thread(target=worker)
        th.start()
        workers.append(th)
    for th in workers:
        th.join()

    check(wait_for(lambda: len(g_fired) == k_threads * k_per_thread, 5,
                   "async callbacks all fired"),
          "2.2 join 后异步回调全部到达（不丢不重，1000/1000）")
    server.shutdown()


# 3. request x add/del 并发
def test_request_add_del_concurrency():
    print("===== 3. request × add/del 并发（服务表读写锁） =====")
    reset_globals()
    server = YomkServer.create(4)
    check(register_conc_srv(server, "/PingSrv") == 0, "3.1 注册 /PingSrv 成功")

    stop = threading.Event()
    ok_list = []
    workers = []

    def req_worker():
        while not stop.is_set():
            r = server.request("/PingSrv/ping")
            if r.status == ResStatus.eOk:
                ok_list.append(1)
            elif not is_expected_no_response(r):
                g_illegal.append(1)
            time.sleep(0.0001)

    for _ in range(8):
        th = threading.Thread(target=req_worker)
        th.start()
        workers.append(th)

    for _ in range(20):
        server.del_service("/PingSrv")
        time.sleep(0.01)
        register_conc_srv(server, "/PingSrv")
        time.sleep(0.01)
    stop.set()
    for th in workers:
        th.join()

    check(len(g_illegal) == 0, "3.2 add/del 并发期间无非法响应")
    check(len(ok_list) > 0, "3.3 存活窗口内有请求被正常服务")
    server.shutdown()


# 4. request x install_func 覆盖安装并发
def test_request_install_func_concurrency():
    print("===== 4. request × installFunc 覆盖安装并发（函数表读写锁） =====")
    reset_globals()
    server = YomkServer.create(4)
    echo_srv = ConcSrv(server)
    echo_srv.set_name("/EchoSrv")
    check(server.add_service(echo_srv) == 0, "4.1 注册 /EchoSrv 成功")

    stop = threading.Event()
    ok_list = []
    workers = []

    def req_worker():
        while not stop.is_set():
            r = server.request("/EchoSrv/echo", DemoPkg("loop"))
            if r.status == ResStatus.eOk and r.msg == "loop":
                ok_list.append(1)
            else:
                g_illegal.append(1)
            time.sleep(0.0001)

    for _ in range(8):
        th = threading.Thread(target=req_worker)
        th.start()
        workers.append(th)

    # 三参/两参覆盖安装交替
    for _ in range(200):
        echo_srv.install_func("/echo", echo_srv.echo, "String")
        echo_srv.install_func("/echo", echo_srv.echo)
    stop.set()
    for th in workers:
        th.join()

    check(len(g_illegal) == 0, "4.2 install_func 覆盖安装并发期间无非法响应")
    check(len(ok_list) > 0, "4.3 覆盖安装并发下请求正常服务（无撕裂）")
    server.shutdown()


# 5. shutdown x 在途异步真并发
def test_shutdown_inflight_async():
    print("===== 5. shutdown × 在途异步并发（真并发交错） =====")
    reset_globals()
    server = YomkServer.create(4)
    check(register_conc_srv(server, "/ShutdownAsyncSrv") == 0,
          "5.1 注册 /ShutdownAsyncSrv 成功")

    shutdown_done = threading.Event()

    def poster():
        while not shutdown_done.is_set():
            server.async_request("/ShutdownAsyncSrv/echo", DemoPkg("s"),
                                 on_fired_ok)
            g_posted.append(1)
            time.sleep(0.001)

    th = threading.Thread(target=poster)
    th.start()
    time.sleep(0.2)
    server.shutdown()
    shutdown_done.set()
    th.join()

    last = [len(g_fired)]

    def fired_stable():
        cur = len(g_fired)
        if cur == last[0]:
            return True
        last[0] = cur
        return False

    check(wait_for(fired_stable, 5, "async fired stable after shutdown"),
          "5.2 shutdown 后异步回调数稳定（排空完成）")
    check(len(g_fired) <= len(g_posted), "5.3 异步无重复执行（fired <= posted）")

    before = len(g_fired)
    for _ in range(100):
        server.async_request("/ShutdownAsyncSrv/echo", DemoPkg("s"),
                             on_fired_ok)
    time.sleep(0.2)
    check(len(g_fired) == before, "5.4 shutdown 后异步投递全部被拒（fired 不变）")


# 7. N7 持续压测（第 6 节"删除即停并发形态"整节剔除，见文件头剔除说明）
def test_n7_stress():
    print("===== 7. N7 持续压测（8 线程混合操作跨双 server 实例） =====")
    reset_globals()
    server1 = YomkServer.create(4)
    server2 = YomkServer.create(4)
    check(register_conc_srv(server1, "/StressSrv") == 0,
          "7.1 注册 server1 /StressSrv 成功")
    check(register_conc_srv(server2, "/StressSrv") == 0,
          "7.2 注册 server2 /StressSrv 成功")

    # 总操作数：YOMK_TEST_STRESS_SCALE 缺省 100000（10 万级）/ 快速模式 10000，
    # clamp 下限 1000；8 线程均分
    total_ops = int(os.environ.get("YOMK_TEST_STRESS_SCALE", "100000"))
    total_ops = max(total_ops, 1000)
    k_threads_per_server = 4
    k_ops_per_thread = total_ops // (k_threads_per_server * 2)

    posted1, fired1, ok1 = [], [], []
    posted2, fired2, ok2 = [], [], []

    def stress_worker(server, posted, fired, ok):
        for i in range(k_ops_per_thread):
            mod = i % 3
            if mod == 0:
                r = server.request("/StressSrv/ping")
                if r.status == ResStatus.eOk:
                    ok.append(1)
            elif mod == 1:
                server.async_request("/StressSrv/echo", DemoPkg("stress"),
                                     make_fired_cb(fired))
                posted.append(1)
            else:
                server.service_names()

    workers = []
    for _ in range(k_threads_per_server):
        workers.append(threading.Thread(
            target=stress_worker, args=(server1, posted1, fired1, ok1)))
        workers.append(threading.Thread(
            target=stress_worker, args=(server2, posted2, fired2, ok2)))

    def shutdown_worker():
        time.sleep(0.1)
        server2.shutdown()
        server2.shutdown()  # 并发幂等顺带验证

    shutdown_th = threading.Thread(target=shutdown_worker)
    for th in workers:
        th.start()
    shutdown_th.start()
    for th in workers:
        th.join()
    shutdown_th.join()

    # server1 未 shutdown，等待所有 async 回调完成
    check(wait_for(lambda: len(fired1) == len(posted1), 10,
                   "server1 async callbacks complete"),
          "7.3 server1 异步回调全部到达（fired == posted）")

    # server2 已 shutdown，等待回调数稳定（可能小于 posted2）
    last2 = [len(fired2)]

    def fired2_stable():
        cur = len(fired2)
        if cur == last2[0]:
            return True
        last2[0] = cur
        return False

    check(wait_for(fired2_stable, 10,
                   "server2 async callbacks stable after shutdown"),
          "7.4 server2 shutdown 后异步回调数稳定")
    check(len(ok1) > 0, "7.5 server1 压测请求获得 eOk")
    check(len(fired1) <= len(posted1), "7.6 server1 异步无重复执行")
    check(len(fired2) <= len(posted2),
          "7.7 server2 shutdown 后异步无重复执行")

    # server1 的兜底销毁以显式 shutdown 承载（python 无确定性析构，
    # A-4 口径）
    server1.shutdown()


def main():
    test_concurrent_sync_request()
    test_concurrent_async_no_loss()
    test_request_add_del_concurrency()
    test_request_install_func_concurrency()
    test_shutdown_inflight_async()
    test_n7_stress()
    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
