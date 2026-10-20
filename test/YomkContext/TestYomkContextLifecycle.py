"""
TestYomkContextLifecycle.py —— YomkContext 服务生命周期与异常安全白盒测试（Context 模块 MC3）

覆盖内容：
1. init 效果（内省）：内置 /YomkContext 注册、init 装配 13 个功能函数（键集合逐一对齐）
2. 基线运行：异步 monitor 池正常工作
3. del_service 触发 deinit + 排空：删除前投递的异步 monitor 全部排空执行
4. 删除后不可路由 + 重复删除契约：请求返回 service not found、重复删除返回 -1
5. 重新注册 + monitor 池重建：new_service(YomkContext) 后 init 重新装配、池重建可用
6. 异常安全：派生服务 init() 抛异常，add_service try/catch 回滚无残留

语言机制说明（非功能缺口）：
- 派生服务 init() 抛 MemoryError 等价承载异常安全
  （add_service 以 try/except Exception 统一回滚，见 YomkServerPy.add_service）
- 重新注册直接以 YomkServerPy.YomkContext 类名传门面 new_service

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
全程使用门面单例拉起内置 /YomkContext；末尾调用 shutdown 干净 deinit 重注册实例
（python 门面 shutdown 后 g_server 置 None、init 不再重建）
"""

import sys
import time

from YomkApi import (
    add_service,
    context_create,
    context_info_keys,
    context_set,
    context_set_monitor,
    context_turn_off_monitor,
    context_turn_on_monitor,
    del_service,
    init,
    new_service,
    request,
    server,
    shutdown,
)
from YomkServerPy import ResStatus, YomkContext

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
    # 10ms 步进轮询等待计数达到 target
    for _ in range(int(timeout_s / 0.01)):
        if len(bucket) >= target:
            return True
        time.sleep(0.01)
    return len(bucket) >= target


class ThrowContext(YomkContext):
    # init() 抛异常验证 add_service 的 try/catch 回滚路径
    #
    def init(self):
        raise MemoryError("mock bad_alloc")


# init 装配的 13 个功能函数
EXPECTED_FUNCS = {
    "/create", "/destroy", "/get", "/set",
    "/turn_on_checker", "/turn_off_checker", "/set_checker",
    "/turn_on_monitor", "/turn_off_monitor", "/set_monitor",
    "/keys", "/key", "/all",
}

# ---- 文件级观测变量 ----
g_async_calls = []  # 异步 monitor 被调用计数


def main():
    srv = init(1)
    check(srv is not None, "init 返回非空服务器")

    # ============ Section 1: init 效果（内省）============
    names = srv.service_names()
    check("/YomkContext" in names, "内置 /YomkContext 已注册")

    infos = srv.service_func_infos("/YomkContext")
    check(len(infos) == 13, "init 装配 13 个功能函数")
    check(set(infos.keys()) == EXPECTED_FUNCS, "装配的功能函数键集合逐一对齐")

    # ============ Section 2: 基线运行（异步 monitor 池工作）============
    context_turn_on_monitor()
    resp = context_create("life_base", "b0")
    check(resp.status == ResStatus.eOk, "创建 life_base 成功")

    g_async_calls.clear()
    context_set_monitor("life_base", lambda c: g_async_calls.append(1), True)

    context_set("life_base", "b1")
    context_set("life_base", "b2")

    reached = wait_for_count(g_async_calls, 2, 2.0)
    check(reached and len(g_async_calls) == 2,
          "基线：异步 monitor 池正常工作（计数 == 2）")
    context_turn_off_monitor()

    # ============ Section 3: del_service 触发 deinit + 排空 ============
    context_turn_on_monitor()
    resp = context_create("life_drain", "d0")
    check(resp.status == ResStatus.eOk, "创建 life_drain 成功")

    g_async_calls.clear()
    context_set_monitor("life_drain", lambda c: g_async_calls.append(1), True)

    n = 50
    for i in range(n):
        context_set("life_drain", f"d{i}")

    # 立即删除服务：触发 deinit 排空 monitor 池
    del_ret = del_service("/YomkContext")
    check(del_ret == 0, "del_service(/YomkContext) 返回 0")

    # del_service 返回后排空已完成，计数应等于投递次数（无丢失、无迟到）
    check(len(g_async_calls) == n,
          "del_service 排空后异步 monitor 计数 == 投递次数")
    context_turn_off_monitor()

    # ============ Section 4: 删除后不可路由 + 重复删除契约 ============
    resp = request("/YomkContext/keys", None)
    check(resp.status == ResStatus.eNo, "删除后请求 /YomkContext 返回 eNo")
    check(resp.msg == "service not found: /YomkContext",
          "删除后消息为 service not found: /YomkContext")

    names = server().service_names()
    check("/YomkContext" not in names, "serviceNames 不再包含 /YomkContext")

    del_again = del_service("/YomkContext")
    check(del_again == -1, "重复删除不存在服务返回 -1")

    # ============ Section 5: 重新注册 + monitor 池重建 ============
    re_add = new_service(YomkContext, "/YomkContext")
    check(re_add == 0, "重新注册 YomkContext 返回 0")

    infos = server().service_func_infos("/YomkContext")
    check(len(infos) == 13, "重新注册后 init 重新装配 13 个功能函数")

    context_turn_on_monitor()
    resp = context_create("life_rebuild", "r0")
    check(resp.status == ResStatus.eOk, "重新注册后创建 life_rebuild 成功")

    g_async_calls.clear()
    context_set_monitor(
        "life_rebuild", lambda c: g_async_calls.append(1), True)

    context_set("life_rebuild", "r1")
    context_set("life_rebuild", "r2")
    context_set("life_rebuild", "r3")

    reached = wait_for_count(g_async_calls, 3, 2.0)
    check(reached and len(g_async_calls) == 3,
          "重新注册后 monitor 池重建可用（计数 == 3）")
    context_turn_off_monitor()

    # ============ Section 6: 异常安全（init 抛异常回滚）============
    add_ret = add_service(ThrowContext(server()), "/ThrowContext")
    check(add_ret == -1, "init 抛异常时 add_service 返回 -1")

    names = server().service_names()
    check("/ThrowContext" not in names,
          "回滚后 serviceNames 不含 /ThrowContext（无残留）")

    # 进程存活、/YomkContext 仍可用
    resp = context_info_keys()
    check(resp.status == ResStatus.eOk, "异常回滚后 /YomkContext 仍可用")

    shutdown()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
