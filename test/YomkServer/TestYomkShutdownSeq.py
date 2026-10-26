"""
TestYomkShutdownSeq.py —— async_request + shutdown 排空/幂等/拒新语义测试（正式全面测试）

覆盖内容：
1. 排空语义：shutdown 阻塞至在途异步任务全部执行完毕才返回；池停止后服务
   deinit 被调用一次
2. 拒新：shutdown 后 async_request 被拒（回调永不触发，日志 "async request
   ignored"）；shutdown 后同步 request 返回 "service not found"（现状文档化：
   表已清空，非专用文案）
3. 幂等：连续两次 shutdown 仅一次 deinit（shutdown_done 标志保证不双重清理）
4. 嵌套排空拒绝：排空期嵌套发起的 asyncRequest 因 shutdown 已置位被拒绝，
   不无限排空

语言机制说明（非功能缺口）：
- 弱绑定判活已对齐落地（YomkService.weak_func + deleted/mark_deleted）：
  install_func 自动弱绑定，排空期弱绑定回调照常执行（shutdown 不置位注销标志），
  与 del_service 删除即停对比实证
- 析构兜底不适用：python 无确定性析构，兜底语义由显式 shutdown 与进程退出 atexit 承载

说明：call_once 仅约束 YomkAPI.init 单例路径；本测试在类级直测 YomkServer，
各 YomkServer.create 实例自持独立 shutdown 标志互不影响，单进程多实例即可完成
全部场景（API 级 shutdown 归门面生命周期测试）；零源码变更

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys
import time

from YomkServerPy import ResStatus, YomkResponse, YomkServer, YomkService

g_failed = 0

# ---- 文件级观测变量（每节首重置；写于池线程、读于 shutdown join 之后，无并发读写） ----
g_task_count = 0    # 异步任务回调到达计数

g_deinit_count = 0  # 服务 deinit 调用次数
g_spawn_exec = 0    # /spawn 真实执行次数
g_deinit_deleted = None  # deinit 时服务注销标志状态（shutdown 路径应为 False）


def reset_globals():
    global g_task_count, g_deinit_count, g_spawn_exec, g_deinit_deleted
    g_task_count = 0
    g_deinit_count = 0
    g_spawn_exec = 0
    g_deinit_deleted = None


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


class DrainSrv(YomkService):
    """排空演示服务：/ping 固定响应、/work 占位；deinit 记录计数"""

    def init(self):
        self.install_func("/ping", self.ping)
        self.install_func("/work", self.work)
        return 0

    def deinit(self):
        global g_deinit_count, g_deinit_deleted
        g_deinit_count += 1
        g_deinit_deleted = self.deleted()

    def ping(self, pkg):
        return YomkResponse(ResStatus.eOk, "pong")

    def work(self, pkg):
        return YomkResponse(ResStatus.eOk, "work")


class SpawnSrv(YomkService):
    """嵌套投递演示服务：/spawn 执行时嵌套发起自身异步请求

    休眠 200ms 保证嵌套投递发生在主线程进入 shutdown 之后的排空期（拒绝路径）；
    守护上限 50 保证即使拒绝承诺失效也不至无限循环挂起，失败形态为断言
    """

    def init(self):
        self.install_func("/spawn", self.spawn)
        return 0

    def spawn(self, pkg):
        global g_spawn_exec
        g_spawn_exec += 1
        time.sleep(0.2)
        if g_spawn_exec < 50 and self.server:
            self.server.async_request("/SpawnSrv/spawn")
        return YomkResponse(ResStatus.eOk, "spawn")


def on_task(resp):
    global g_task_count
    g_task_count += 1


# 1. 排空语义：shutdown 阻塞至在途任务全部执行
def test_drain_semantics():
    server = YomkServer.create(2)
    reset_globals()
    check(server.new_service(DrainSrv, "/DrainSrv") == 0, "排空用例：注册服务成功")

    for _ in range(100):
        server.async_request("/DrainSrv/ping", None, on_task)

    server.shutdown()  # 阻塞至请求池排空 join 完成

    check(g_task_count == 100,
          "排空：100 个在途弱绑定异步任务在 shutdown 返回前全部执行（install_func 自动包装存活路径透明）")
    check(g_deinit_count == 1, "排空：池停止后服务 deinit 被调用一次")
    check(g_deinit_deleted is False,
          "排空：shutdown 不置位注销标志（deinit 时 deleted() 为 False，与 del_service 删除即停对比）")


# 2. 拒新：shutdown 后异步投递被拒、回调永不触发；同步 request 现状文档化
def test_reject_after_shutdown():
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(DrainSrv, "/DrainSrv") == 0, "拒新用例：注册服务成功")
    server.shutdown()

    reject_cb_count = 0

    def on_reject(resp):
        nonlocal reject_cb_count
        reject_cb_count += 1

    server.async_request("/DrainSrv/ping", None, on_reject)
    time.sleep(0.2)
    check(reject_cb_count == 0,
          "拒新：shutdown 后 async_request 回调永不触发（日志 async request ignored 实证）")

    resp = server.request("/DrainSrv/ping")
    check(resp.status == ResStatus.eNo and "service not found" in resp.msg,
          "现状文档化：shutdown 后同步 request 返回 eNo + service not found（表已清空，非专用文案）")
    check(server.service_names() == [], "shutdown 后服务表已清空")


# 3. 幂等：连续两次 shutdown 仅一次 deinit，无挂起无崩溃
def test_shutdown_idempotent():
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(DrainSrv, "/DrainSrv") == 0, "幂等用例：注册服务成功")

    server.shutdown()
    server.shutdown()  # 第二次：shutdown_done 标志已置位直接返回

    check(g_deinit_count == 1, "幂等：连续两次 shutdown 仅一次 deinit（不双重清理）")


# 4. 嵌套排空拒绝：排空期嵌套投递被 shutdown 标志拒绝，不无限排空
def test_nested_spawn_rejected():
    server = YomkServer.create(1)
    reset_globals()
    check(server.new_service(SpawnSrv, "/SpawnSrv")
          == 0, "嵌套排空用例：注册 SpawnSrv 成功")

    server.async_request("/SpawnSrv/spawn")
    server.shutdown()  # /spawn 休眠 200ms 后嵌套投递，此刻 shutdown 标志已置位应被拒绝

    check(g_spawn_exec == 1,
          "嵌套排空拒绝：/spawn 仅执行一次（排空期嵌套投递被拒绝，不无限排空）")


def main():
    test_drain_semantics()
    test_reject_after_shutdown()
    test_shutdown_idempotent()
    test_nested_spawn_rejected()

    if g_failed > 0:
        print(f"TestYomkShutdownSeq failed, count: {g_failed}")
        return 1
    print("TestYomkShutdownSeq all check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
