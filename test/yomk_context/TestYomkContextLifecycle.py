"""
TestYomkContextLifecycle.py —— YomkContext 服务生命周期与异常安全白盒测试（Context 模块 MC3）

覆盖内容：
1. init 效果（内省）：内置 /YomkContext 注册、init 装配 14 个功能函数（键集合逐一对齐）
2. 常规运行：异步 monitor 池正常工作
3. del_service 触发 deinit + 排空：删除前投递的异步 monitor 全部排空执行
4. 删除后不可路由 + 重复删除契约：请求返回 service not found、重复删除返回 -1
5. 重新注册 + monitor 池重建：new_service(YomkContext) 后 init 重新装配、池重建可用
6. 异常安全：派生服务 init() 抛异常，add_service try/catch 回滚无残留

语言机制说明（非功能缺口）：
- 派生服务 init() 抛 MemoryError 等价承载异常安全
  （add_service 以 try/except Exception 统一回滚，见 YomkServer.add_service）
- 重新注册直接以 YomkContext 类名传门面 new_service

风格：纯 main() + 失败计数，退出码非 0 表示存在失败用例（零第三方依赖）
全程使用门面单例拉起内置 /YomkContext；末尾调用 shutdown 干净 deinit 重注册实例
（python 门面 shutdown 后 g_server 置 None、init 不再重建）
"""

import sys
import time

from yomk.msg.server import ResStatus
from yomk.server import YomkServer, server
from yomk.ctx import ctx
from yomk.ctx.YomkContext import YomkContext  # 白盒测试直用内部类，非公开面


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

# init 装配的 14 个功能函数
EXPECTED_FUNCS = {
    "/create", "/destroy", "/get", "/set",
    "/turn_on_checker", "/turn_off_checker", "/set_checker",
    "/turn_on_monitor", "/turn_off_monitor", "/set_monitor",
    "/groups", "/keys", "/key", "/all",
}

# ---- 文件级观测变量 ----
g_async_calls = []  # 异步 monitor 被调用计数

def main():
    server().async_thread(1).init()
    srv = server().instance()
    check(srv is not None, "init 返回非空服务器")

    # ============ Section 1: init 效果（内省）============
    names = srv.service_names()
    check("/YomkContext" in names, "内置 /YomkContext 已注册")

    infos = srv.service_func_infos("/YomkContext")
    check(len(infos) == 14, "init 装配 14 个功能函数")
    check(set(infos.keys()) == EXPECTED_FUNCS, "装配的功能函数键集合逐一对齐")

    # ============ Section 2: 常规运行（异步 monitor 池工作）============
    ctx().monitor().enable()
    resp = ctx().key("life_base").value("b0").create()
    check(resp == ResStatus.eOk, "创建 life_base 成功")

    g_async_calls.clear()
    ctx().key("life_base").monitor_fn(lambda c: g_async_calls.append(1)).async_(True).apply()

    ctx().key("life_base").value("b1").set()
    ctx().key("life_base").value("b2").set()

    reached = wait_for_count(g_async_calls, 2, 2.0)
    check(reached and len(g_async_calls) == 2,
          "基线：异步 monitor 池正常工作（计数 == 2）")
    ctx().monitor().disable()

    # ============ Section 3: del_service 触发 deinit + 排空 ============
    ctx().monitor().enable()
    resp = ctx().key("life_drain").value("d0").create()
    check(resp == ResStatus.eOk, "创建 life_drain 成功")

    g_async_calls.clear()
    ctx().key("life_drain").monitor_fn(lambda c: g_async_calls.append(1)).async_(True).apply()

    n = 50
    for i in range(n):
        ctx().key("life_drain").value(f"d{i}").set()

    # 立即删除服务：触发 deinit 排空 monitor 池
    del_ret = server().srv_name("/YomkContext").remove()
    check(del_ret == 0, "del_service(/YomkContext) 返回 0")

    # del_service 返回后排空已完成，计数应等于投递次数（无丢失、无迟到）
    check(len(g_async_calls) == n,
          "del_service 排空后异步 monitor 计数 == 投递次数")
    ctx().monitor().disable()

    # ============ Section 4: 删除后不可路由 + 重复删除契约 ============
    resp = server().url("/YomkContext/keys").request()
    check(resp.status == ResStatus.eNo, "删除后请求 /YomkContext 返回 eNo")
    check(resp.msg == "service not found: /YomkContext",
          "删除后消息为 service not found: /YomkContext")

    names = server().instance().service_names()
    check("/YomkContext" not in names, "serviceNames 不再包含 /YomkContext")

    del_again = server().srv_name("/YomkContext").remove()
    check(del_again == -1, "重复删除不存在服务返回 -1")

    # ============ Section 5: 重新注册 + monitor 池重建 ============
    re_add = server().srv(YomkContext, "/YomkContext").create()
    check(re_add == 0, "重新注册 YomkContext 返回 0")

    infos = server().instance().service_func_infos("/YomkContext")
    check(len(infos) == 14, "重新注册后 init 重新装配 14 个功能函数")

    ctx().monitor().enable()
    resp = ctx().key("life_rebuild").value("r0").create()
    check(resp == ResStatus.eOk, "重新注册后创建 life_rebuild 成功")

    g_async_calls.clear()
    ctx().key("life_rebuild").monitor_fn(lambda c: g_async_calls.append(1)).async_(True).apply()

    ctx().key("life_rebuild").value("r1").set()
    ctx().key("life_rebuild").value("r2").set()
    ctx().key("life_rebuild").value("r3").set()

    reached = wait_for_count(g_async_calls, 3, 2.0)
    check(reached and len(g_async_calls) == 3,
          "重新注册后 monitor 池重建可用（计数 == 3）")
    ctx().monitor().disable()

    # ============ Section 6: 异常安全（init 抛异常回滚）============
    add_ret = server().srv_ptr(ThrowContext(server().instance())).srv_name("/ThrowContext").add()
    check(add_ret == -1, "init 抛异常时 add_service 返回 -1")

    names = server().instance().service_names()
    check("/ThrowContext" not in names,
          "回滚后 serviceNames 不含 /ThrowContext（无残留）")

    # 进程存活、/YomkContext 仍可用
    resp = ctx().keys()
    check(resp.status == ResStatus.eOk, "异常回滚后 /YomkContext 仍可用")

    server().shutdown()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED")
          + f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
