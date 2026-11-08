#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkUmbrella.py —— 新包六域公开面锚点 + 冒烟（L12 起）

本文件是**运行期公开面锚点**测试：
显式 import 六域入口（不再经伞门面 YomkApi），断言命名空间锚点
（yomk / yomk.msg 顶层无 __init__）、各域 builder 公开方法面与
各域 builder.py 定型清单逐一对表，再走 S1-S8 六域冒烟。

语言机制说明：
- 日志发射走链式 API：S5 经链式调用发射并返回 Succeeded；
- String 消息以裸 str 承载、服务实例直接传递（消息包/持有语义的框架形态）；
- S8 关闭守卫：shutdown 后 instance() 返回 None；
- 旧伞面 ServerBuilder 的 services/functions/fn_url/get_info/all 四查询
  已迁至 srv_info 域（SrvInfoBuilder），锚点表按新归属对表；
- 收尾打印 `ALL PASSED` / `SOME FAILED` + `sys.exit(g_failed)`
  （非全仓统一的 "all check passed." 行——本文件即如此）。

纪律：只 import yomk.* 域入口 + stdlib（本文件出现 YomkApi /
顶层 import YomkServer 即违例；YomkServer 经 from yomk.server import
直取属正常白盒面）。
"""

import inspect
import sys

import yomk
import yomk.msg
from yomk.server import server, YomkBoot, YomkServer, YomkService
from yomk.ctx import ctx
from yomk.fn_pool import fn_pool
from yomk.event_loop import event_loop
from yomk.log import log, log_mgr
from yomk.srv_info import srv_info
from yomk.msg.server import ResStatus, Response

g_failed = 0

# 域入口函数表（锚点段统一经此取入口）
HEADS = {
    "server": server, "ctx": ctx, "fn_pool": fn_pool,
    "event_loop": event_loop, "log": log, "log_mgr": log_mgr,
    "srv_info": srv_info,
}

# 域入口 → (builder 类型名, 公开方法清单)——与各域 src/yomk/<域>/builder.py 实测对表
EXPECTED_HEADS = {
    "server": ("ServerBuilder", [
        "url", "msg", "request", "status", "async_", "version", "async_thread",
        "init", "instance", "shutdown", "srv", "create", "srv_name", "srv_ptr",
        "add", "remove", "boot_ptr", "run"]),
    "ctx": ("ContextBuilder", [
        "key", "group", "value", "create", "set", "destroy", "checker_fn",
        "monitor_fn", "async_", "apply", "checker", "monitor", "keys", "info",
        "groups", "all", "msg"]),
    "fn_pool": ("FnPoolBuilder", [
        "fn_name", "fn", "msg_name", "description", "msg", "install",
        "uninstall", "call", "list", "get_info", "all"]),
    "event_loop": ("EventLoopBuilder", [
        "loop", "handle_fn", "msg_name", "msg", "tag", "tag_count", "start",
        "stop", "post", "post_wait", "destroy", "list", "get_info", "all"]),
    "log": ("LogBuilder", [
        "tag", "to_sink", "info", "warn", "error", "debug", "trace", "fatal"]),
    "log_mgr": ("LogMgrBuilder", [
        "dir", "file", "console", "location", "all"]),
    "srv_info": ("SrvInfoBuilder", [
        "srv_name", "fn_url", "services", "functions", "get_info", "all"]),
}

# 分面类型（不发起请求的纯构造切入）
EXPECTED_FACETS = [
    ("AsyncRequestBuilder", lambda: server().async_(None)),
    ("GroupSwitch", lambda: ctx().group("main").checker()),
    ("ContextValueBuilder", lambda: ctx().key("k").msg()),
    ("SinkLogBuilder", lambda: log().to_sink("s")),
    ("ConsoleMgrBuilder", lambda: log_mgr().console("c")),
    ("FileMgrBuilder", lambda: log_mgr().file("f")),
    ("LocBuilder", lambda: log_mgr().location()),
]


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def public_methods(obj):
    return sorted(name for name, _ in inspect.getmembers(
        obj, predicate=inspect.ismethod) if not name.startswith("_"))


class UmbrellaService(YomkService):
    def init(self):
        return 0

    def deinit(self):
        return 0


class UmbrellaBoot(YomkBoot):
    def before(self):
        return 0

    def start(self):
        return 0

    def after(self):
        return 0


def umb_echo(pkg):
    # 注册函数约定返回 Response（与 TestYomkFunctionPoolLifecycle.echo_func 同形）
    return Response(ResStatus.eOk, "echo", pkg)


def main():
    Succeeded = ResStatus.Succeeded

    # ============ 锚点段：命名空间 + 七域入口类型与公开方法对表 ============
    check(yomk.__file__ is None, "锚点: yomk 顶层无 __init__（__file__ is None）")
    check(yomk.msg.__file__ is None, "锚点: yomk.msg 层无 __init__（__file__ is None）")

    anchor_ok = True
    for head_name, (type_name, methods) in EXPECTED_HEADS.items():
        head = HEADS[head_name]
        if head is None or not callable(head):
            anchor_ok = False
            print(f"       域入口缺失: {head_name}()")
            continue
        builder = head()
        if type(builder).__name__ != type_name:
            anchor_ok = False
            print(f"       {head_name}() 返回 {type(builder).__name__} != {type_name}")
            continue
        actual = public_methods(builder)
        if actual != sorted(methods):
            anchor_ok = False
            missing = sorted(set(methods) - set(actual))
            extra = sorted(set(actual) - set(methods))
            print(f"       {type_name} 方法面漂移 missing={missing} extra={extra}")
    check(anchor_ok, "锚点: 七域入口 builder 类型与公开方法面逐一对表")

    facet_ok = True
    for type_name, make in EXPECTED_FACETS:
        obj = make()
        if type(obj).__name__ != type_name:
            facet_ok = False
            print(f"       分面 {type_name} 实际 {type(obj).__name__}")
    check(facet_ok, "锚点: 七个分面类型构造切入正确（零请求副作用）")
    check(all(hasattr(YomkBoot, m) for m in ("before", "start", "after")),
          "锚点: YomkBoot 抽象三钩子在册")

    # ============ S1 生命周期 / 版本 / 裸请求 ============
    server().async_thread(1).init()
    inst = server().instance()
    check(isinstance(inst, YomkServer), "S1: init 后 instance() 为 YomkServer")
    server().init()  # 幂等：不应重建
    check(server().instance() is inst, "S1: 二次 init 幂等返回既有实例")
    ver = server().version()
    check(isinstance(ver, str) and ver and ver != "unknown", f"S1: version() = {ver}")
    res = server().url("/YomkServerInfo/services").request()
    check(res.status == ResStatus.eOk and res.data, "S1: 裸请求 services 非空")

    # ============ S2 服务管理 ============
    ret = server().create()
    check(ret == -1, "S2: 未先 srv() 的 create() == -1（未累积服务类，守卫走 diag 提示）")
    ret = server().srv(UmbrellaService, "umb_srv").create()
    check(ret == 0, "S2: srv().create() == 0")
    # srv_ptr/add 分面：直构造服务实例（YomkService 构造需 server 引用，实例直传）
    svc = UmbrellaService(server().instance())
    ret = server().srv_name("umb_add").srv_ptr(svc).add()
    check(ret == 0, "S2: srv_ptr().add() == 0")
    res = srv_info().services()
    names = res.data if isinstance(res.data, list) else []
    check("umb_srv" in names and "umb_add" in names, f"S2: services 含两新服务 {names}")
    ret = server().srv_name("umb_srv").remove()
    check(ret == 0, "S2: remove(umb_srv) == 0")
    ret = server().srv_name("umb_add").remove()
    check(ret == 0, "S2: remove(umb_add) == 0")

    # ============ S3 boot 编排 ============
    ret = server().boot_ptr(UmbrellaBoot()).run()
    check(ret == 0, "S3: boot_ptr().run() == 0")

    # ============ S4 上下文 ============
    ret = ctx().key("umb_k").group("main").value("v1").create()
    check(ret == Succeeded, "S4: ctx create Succeeded")
    ret = ctx().key("umb_k").group("main").value("v2").set()
    check(ret == Succeeded, "S4: ctx set Succeeded")
    got = ctx().key("umb_k").group("main").msg().with_default("x").get()
    check(got == "v2", f"S4: 类型化读回 == v2（实际 {got!r}）")
    res = ctx().group("main").keys()
    check(res.status == ResStatus.eOk and "umb_k" in (res.data or []),
          f"S4: keys() 含 umb_k（实际 {res.data!r}）")
    ret = ctx().group("main").checker().enable()
    check(ret == Succeeded, "S4: 组 checker enable Succeeded")
    ret = ctx().key("umb_k").group("main").destroy()
    check(ret == Succeeded, "S4: ctx destroy Succeeded")

    # ============ S5 日志发射（链式发射返回 Succeeded） ============
    ret = log().tag("umbrella").info("umb s5 emit")
    check(ret == Succeeded, "S5: log().tag().info() Succeeded")
    ret = log_mgr().location().apply()
    check(ret == Succeeded, "S5: location().apply() 恒 Succeeded")

    # ============ S6 函数池 ============
    ret = fn_pool().fn_name("/umb_echo").fn(umb_echo).msg_name(
        "UmbMsg").description("umbrella s6").install()
    check(ret == Succeeded, "S6: install Succeeded")
    res = fn_pool().fn_name("/umb_echo").get_info()
    info = res.data
    check(res.status == ResStatus.eOk and info is not None and
          info.func_name == "/umb_echo" and info.msg_name == "UmbMsg" and
          info.description == "umbrella s6",
          f"S6: get_info 三字段对表（实际 {info}）")
    res = fn_pool().list()
    check(res.status == ResStatus.eOk and "/umb_echo" in (res.data or []),
          f"S6: list 含 /umb_echo（实际 {res.data!r}）")
    res = fn_pool().fn_name("/umb_echo").msg("ping").call()
    check(res.status == ResStatus.eOk and res.msg == "echo" and
          res.data == "ping",
          f"S6: call 阻塞返回 msg==echo/data 透传（实际 {res.msg!r}/{res.data!r}）")
    res = fn_pool().all()
    check(res.status == ResStatus.eOk and res.data, "S6: all 非空")
    ret = fn_pool().fn_name("/umb_echo").uninstall()
    check(ret == Succeeded, "S6: uninstall Succeeded")

    # ============ S7 事件循环 ============
    ret = event_loop().loop("umb_loop").handle_fn(None).msg_name("").start()
    check(ret == Succeeded, "S7: 无默认 handler start Succeeded")
    ret = event_loop().loop("umb_loop").msg("hi").post_wait()
    check(ret == Succeeded, "S7: post_wait Succeeded（无 handler 静默出队）")
    res = event_loop().list()
    check(res.status == ResStatus.eOk and "umb_loop" in (res.data or []),
          f"S7: list 含 umb_loop（实际 {res.data!r}）")
    ret = event_loop().loop("umb_loop").destroy()
    check(ret == Succeeded, "S7: destroy Succeeded")

    # ============ S8 关闭守卫 ============
    server().shutdown()
    check(server().instance() is None,
          "S8: 关闭后 instance() is None（单例置空）")
    res = srv_info().all()
    check(res.status == ResStatus.Invalid and
          res.msg == "YomkServer is not init",
          f"S8: 关闭后 all() Invalid + 契约串（实际 {res.status}/{res.msg!r}）")

    print("ALL PASSED" if g_failed == 0 else "SOME FAILED")
    return g_failed


if __name__ == "__main__":
    sys.exit(main())
