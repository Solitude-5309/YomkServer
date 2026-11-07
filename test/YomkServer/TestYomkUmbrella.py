#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkUmbrella.py —— 伞门面运行期锚点 + 六域冒烟（闭环 8 S16，
蓝本 C++ Test/YomkServer/TestYomkUmbrella.cpp 137 行）

C++ 蓝本是"只允许 include umbrella 头"的编译期锚点测试；Python 无编译期，
等价物为**运行期公开面锚点**：本文件只 `import YomkApi`（伞门面，不 import
YomkServerPy 本体），断言六域入口返回类型正确、各 builder 公开方法面与
YomkApi.py 定型清单逐一对表，再走 S1-S8 六域冒烟。

Python 适配注记：
- diag 宏豁免：C++ S5 的 YOMK_DIAG_LOG 发射在 Python 以链式发射 Succeeded 代替；
- msgs::String → 裸 str、shared_ptr → 实例直传（消息包/持有语义框架级差异）；
- S8 关闭守卫：instance() 返回 None（C++ 当前基线 shutdown 内同置 nullptr，语义已对齐）；
- 收尾沿用蓝本口径 `ALL PASSED` / `SOME FAILED` + `sys.exit(g_failed)`
  （非全仓统一的 "all check passed." 行——蓝本此文件即如此）。

纪律：只 import YomkApi + stdlib（本文件出现 YomkServerPy 直接 import 即违例）。
"""

import inspect
import sys

import YomkApi

g_failed = 0

# 六域入口 → (builder 类型名, 公开方法清单)——与 src/YomkApi.py ast 实测对表
EXPECTED_HEADS = {
    "server": ("ServerBuilder", [
        "url", "msg", "request", "status", "async_", "version", "async_thread",
        "init", "instance", "shutdown", "srv", "create", "srv_name", "srv_ptr",
        "add", "remove", "boot_ptr", "run", "services", "functions", "fn_url",
        "get_info", "all"]),
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
}

# 分面类型（不发起请求的纯构造切入）
EXPECTED_FACETS = [
    ("AsyncRequestBuilder", lambda: YomkApi.server().async_(None)),
    ("GroupSwitch", lambda: YomkApi.ctx().group("main").checker()),
    ("ContextValueBuilder", lambda: YomkApi.ctx().key("k").msg()),
    ("SinkLogBuilder", lambda: YomkApi.log().to_sink("s")),
    ("ConsoleMgrBuilder", lambda: YomkApi.log_mgr().console("c")),
    ("FileMgrBuilder", lambda: YomkApi.log_mgr().file("f")),
    ("LocBuilder", lambda: YomkApi.log_mgr().location()),
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


class UmbrellaService(YomkApi.YomkService):
    def init(self):
        return 0

    def deinit(self):
        return 0


class UmbrellaBoot(YomkApi.YomkBoot):
    def before(self):
        return 0

    def start(self):
        return 0

    def after(self):
        return 0


def umb_echo(pkg):
    # 注册函数约定返回 YomkResponse（与 TestYomkFunctionPoolLifecycle.echo_func 同形）
    return YomkApi.YomkResponse(YomkApi.ResStatus.eOk, "echo", pkg)


def main():
    Succeeded = YomkApi.ResStatus.Succeeded

    # ============ 锚点段：伞面类型与公开方法对表 ============
    anchor_ok = True
    for head_name, (type_name, methods) in EXPECTED_HEADS.items():
        head = getattr(YomkApi, head_name, None)
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
    check(anchor_ok, "锚点: 六域入口 builder 类型与公开方法面逐一对表")

    facet_ok = True
    for type_name, make in EXPECTED_FACETS:
        obj = make()
        if type(obj).__name__ != type_name:
            facet_ok = False
            print(f"       分面 {type_name} 实际 {type(obj).__name__}")
    check(facet_ok, "锚点: 七个分面类型构造切入正确（零请求副作用）")
    check(all(hasattr(YomkApi.YomkBoot, m) for m in ("before", "start", "after")),
          "锚点: YomkBoot 抽象三钩子在册")

    # ============ S1 生命周期 / 版本 / 裸请求 ============
    YomkApi.server().async_thread(1).init()
    inst = YomkApi.server().instance()
    check(isinstance(inst, YomkApi.YomkServer), "S1: init 后 instance() 为 YomkServer")
    YomkApi.server().init()  # 幂等：不应重建
    check(YomkApi.server().instance() is inst, "S1: 二次 init 幂等返回既有实例")
    ver = YomkApi.server().version()
    check(isinstance(ver, str) and ver and ver != "unknown", f"S1: version() = {ver}")
    res = YomkApi.server().url("/YomkServerInfo/services").request()
    check(res.status == YomkApi.ResStatus.eOk and res.data, "S1: 裸请求 services 非空")

    # ============ S2 服务管理 ============
    ret = YomkApi.server().create()
    check(ret == -1, "S2: 未先 srv() 的 create() == -1（对齐 C++ m_newSvc 守卫，diag 消息）")
    ret = YomkApi.server().srv(UmbrellaService, "umb_srv").create()
    check(ret == 0, "S2: srv().create() == 0")
    # srv_ptr/add 分面：直构造服务实例（YomkService 构造需 server 引用，对齐 C++ shared_ptr 直传）
    svc = UmbrellaService(YomkApi.server().instance())
    ret = YomkApi.server().srv_name("umb_add").srv_ptr(svc).add()
    check(ret == 0, "S2: srv_ptr().add() == 0")
    res = YomkApi.server().services()
    names = res.data if isinstance(res.data, list) else []
    check("umb_srv" in names and "umb_add" in names, f"S2: services 含两新服务 {names}")
    ret = YomkApi.server().srv_name("umb_srv").remove()
    check(ret == 0, "S2: remove(umb_srv) == 0")
    ret = YomkApi.server().srv_name("umb_add").remove()
    check(ret == 0, "S2: remove(umb_add) == 0")

    # ============ S3 boot 编排 ============
    ret = YomkApi.server().boot_ptr(UmbrellaBoot()).run()
    check(ret == 0, "S3: boot_ptr().run() == 0")

    # ============ S4 上下文 ============
    ret = YomkApi.ctx().key("umb_k").group("main").value("v1").create()
    check(ret == Succeeded, "S4: ctx create Succeeded")
    ret = YomkApi.ctx().key("umb_k").group("main").value("v2").set()
    check(ret == Succeeded, "S4: ctx set Succeeded")
    got = YomkApi.ctx().key("umb_k").group("main").msg().with_default("x").get()
    check(got == "v2", f"S4: 类型化读回 == v2（实际 {got!r}）")
    res = YomkApi.ctx().group("main").keys()
    check(res.status == YomkApi.ResStatus.eOk and "umb_k" in (res.data or []),
          f"S4: keys() 含 umb_k（实际 {res.data!r}）")
    ret = YomkApi.ctx().group("main").checker().enable()
    check(ret == Succeeded, "S4: 组 checker enable Succeeded")
    ret = YomkApi.ctx().key("umb_k").group("main").destroy()
    check(ret == Succeeded, "S4: ctx destroy Succeeded")

    # ============ S5 日志发射（diag 宏豁免注记：以链式发射 Succeeded 代替） ============
    ret = YomkApi.log().tag("umbrella").info("umb s5 emit")
    check(ret == Succeeded, "S5: log().tag().info() Succeeded")
    ret = YomkApi.log_mgr().location().apply()
    check(ret == Succeeded, "S5: location().apply() 恒 Succeeded")

    # ============ S6 函数池 ============
    ret = YomkApi.fn_pool().fn_name("/umb_echo").fn(umb_echo).msg_name(
        "UmbMsg").description("umbrella s6").install()
    check(ret == Succeeded, "S6: install Succeeded")
    res = YomkApi.fn_pool().fn_name("/umb_echo").get_info()
    info = res.data
    check(res.status == YomkApi.ResStatus.eOk and info is not None and
          info.func_name == "/umb_echo" and info.msg_name == "UmbMsg" and
          info.description == "umbrella s6",
          f"S6: get_info 三字段对表（实际 {info}）")
    res = YomkApi.fn_pool().list()
    check(res.status == YomkApi.ResStatus.eOk and "/umb_echo" in (res.data or []),
          f"S6: list 含 /umb_echo（实际 {res.data!r}）")
    res = YomkApi.fn_pool().fn_name("/umb_echo").msg("ping").call()
    check(res.status == YomkApi.ResStatus.eOk and res.msg == "echo" and
          res.data == "ping",
          f"S6: call 阻塞返回 msg==echo/data 透传（实际 {res.msg!r}/{res.data!r}）")
    res = YomkApi.fn_pool().all()
    check(res.status == YomkApi.ResStatus.eOk and res.data, "S6: all 非空")
    ret = YomkApi.fn_pool().fn_name("/umb_echo").uninstall()
    check(ret == Succeeded, "S6: uninstall Succeeded")

    # ============ S7 事件循环 ============
    ret = YomkApi.event_loop().loop("umb_loop").handle_fn(None).msg_name("").start()
    check(ret == Succeeded, "S7: 无默认 handler start Succeeded")
    ret = YomkApi.event_loop().loop("umb_loop").msg("hi").post_wait()
    check(ret == Succeeded, "S7: post_wait Succeeded（无 handler 静默出队）")
    res = YomkApi.event_loop().list()
    check(res.status == YomkApi.ResStatus.eOk and "umb_loop" in (res.data or []),
          f"S7: list 含 umb_loop（实际 {res.data!r}）")
    ret = YomkApi.event_loop().loop("umb_loop").destroy()
    check(ret == Succeeded, "S7: destroy Succeeded")

    # ============ S8 关闭守卫 ============
    YomkApi.server().shutdown()
    check(YomkApi.server().instance() is None,
          "S8: 关闭后 instance() is None（C++ 基线同置 nullptr，语义已对齐）")
    res = YomkApi.server().all()
    check(res.status == YomkApi.ResStatus.Invalid and
          res.msg == "YomkServer is not init",
          f"S8: 关闭后 all() Invalid + 契约串（实际 {res.status}/{res.msg!r}）")

    print("ALL PASSED" if g_failed == 0 else "SOME FAILED")
    return g_failed


if __name__ == "__main__":
    sys.exit(main())
