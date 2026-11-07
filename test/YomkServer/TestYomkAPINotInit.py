"""
TestYomkAPINotInit.py —— YomkAPI 未初始化守卫全集测试

覆盖内容：
1. version() 免初始化可调用（不依赖单例，返回非空版本号）
2. 未初始化守卫默认值：server().instance() 为 None、shutdown() 安全空转（幂等）
3. request() 返回 eInvalid + 契约消息 "YomkServer is not init"；async_ 链静默
   投递、回调不触发
4. 服务管理守卫：srv().create()/srv_ptr().add()/srv_name().remove() 返回 -1
5. 链式终结动作守卫抽样：统一守卫入口 _api_request 验证（ctx create/
   fn_pool install/event_loop start 返回 eInvalid + 契约消息同根 +
   ctx 类型化 get 特殊形态原样返回调用方默认值）
6. Logger 域链式终结守卫全量 23 条：
   console/file 代理、六级发射（tag 形态）、create/flush/remove、
   on·off_console/file_log_by_level 两族、loggers/logger/all 内省，
   首条另断契约消息同根

覆盖范围说明：
- "守卫拒绝后调用方资源释放"断言不适用：python 引用计数
  语义无所有权接管概念
- 版本号断言形态：python 版本传递链中 YomkServerPyVersion 模块缺失时回落
  "unknown"（保持门面 import 链可用），故仅断言非空字符串，不断言具体值

说明：门面单例状态（g_server/_init_done）进程级持久，未初始化态须独占进程
（本程序全程不调 init），与 TestYomkAPILifecycle 进程隔离

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys
import time

import YomkApi
from YomkServer import Log, ResStatus, YomkService
from YomkApi import log, log_mgr, ctx, event_loop, fn_pool, server

g_failed = 0
g_async_cb_count = 0  # 未初始化 async_ 链回调到达计数（应恒为 0）

def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1

def on_async_cb(resp):
    global g_async_cb_count
    g_async_cb_count += 1

class DummySrv(YomkService):
    """最小演示服务：未初始化守卫用例载体（守卫先于构造使用，init 不会被调用）"""

    def __init__(self, server=None):
        super().__init__(server)

    def init(self):
        return 0

def main():
    print("===== 1. version 免初始化可调用 =====")
    ver = server().version()
    check(isinstance(ver, str) and ver != "", f"version() 未初始化时返回非空版本号: {ver}")

    print("===== 2. instance() 与未初始化 shutdown 空转 =====")
    check(server().instance() is None, "server_instance() 未初始化返回 None")

    server().shutdown()
    check(server().instance() is None, "shutdown() 未初始化空转，单例保持为空")

    print("===== 3. request()/async_() 未初始化守卫 =====")
    resp = server().url("/Any/func").request()
    check(resp.status == ResStatus.eInvalid, "request 未初始化返回 eInvalid")
    check(resp.msg == "YomkServer is not init",
          f"request 未初始化契约消息: {resp.msg}")

    server().url("/Any/func").async_(on_async_cb).request()
    time.sleep(0.1)
    check(g_async_cb_count == 0, "async_request 未初始化静默返回，回调不触发")

    print("===== 4. 服务管理 API 未初始化守卫 =====")
    check(server().srv(DummySrv).create() == -1, "new_service 未初始化返回 -1")
    check(server().srv_ptr(None).add() == -1, "add_service(None) 未初始化返回 -1")
    check(server().srv_ptr(DummySrv()).add() == -1, "add_service(服务实例) 未初始化返回 -1")
    check(server().srv_name("/Any").remove() == -1, "del_service 未初始化返回 -1")

    print("===== 5. 模块便捷 API 守卫抽样（统一守卫入口验证） =====")
    resp = ctx().key("guard_key").value(None).create()
    check(resp == ResStatus.eInvalid, "context_create 未初始化返回 eInvalid")
    resp = fn_pool().fn_name("guard_func").fn(None).install()
    check(resp == ResStatus.eInvalid,
          "function_pool_register 未初始化返回 eInvalid")
    resp = event_loop().loop("/guard_loop").start()
    check(resp == ResStatus.eInvalid,
          "event_loop_start 未初始化返回 eInvalid")
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息同根改由读路径 Response 承载
    check(event_loop().list().msg == "YomkServer is not init",
          "模块 API 守卫契约消息同根（读路径 event_loop_info_loops）")

    sentinel = object()
    check(ctx().key("guard_key").msg().with_default(sentinel).get() is sentinel,
          "context_get 未初始化原样返回调用方默认值")

    print("===== 6. LOG 模块便捷 API 守卫全量（Logger 模块轮 P1 收口 23 条） =====")
    # Logger 23 条顺序（同第 5 节排布口径）
    check(log_mgr().console().proxy(None).apply() == ResStatus.eInvalid,
          "set_console_log_proxy 未初始化返回 eInvalid")
    resp = log().tag("tag").info("hello")
    check(resp == ResStatus.eInvalid,
          "console_log_info_tag 未初始化返回 eInvalid")
    # 写路径链式终结收敛为 ResStatus（无 msg）；契约消息同根改由读路径 Response 承载
    check(log_mgr().all().msg == "YomkServer is not init",
          "模块 API 守卫契约消息同根（读路径 logger_info_all）")
    check(log().tag("tag").warn("hello") == ResStatus.eInvalid,
          "console_log_warn_tag 未初始化返回 eInvalid")
    check(log().tag("tag").error("hello") == ResStatus.eInvalid,
          "console_log_error_tag 未初始化返回 eInvalid")
    check(log().tag("tag").debug("hello") == ResStatus.eInvalid,
          "console_log_debug_tag 未初始化返回 eInvalid")
    check(log_mgr().dir("/tmp").file("guard.log").create() == ResStatus.eInvalid,
          "file_log_create 未初始化返回 eInvalid")
    check(log_mgr().file("guard.log").flush() == ResStatus.eInvalid,
          "file_log_write 未初始化返回 eInvalid")
    check(log().to_sink("guard.log").tag("tag").info("hello") == ResStatus.eInvalid,
          "file_log_info_tag 未初始化返回 eInvalid")
    check(log().to_sink("guard.log").tag("tag").warn("hello") == ResStatus.eInvalid,
          "file_log_warn_tag 未初始化返回 eInvalid")
    check(log().to_sink("guard.log").tag("tag").error("hello") == ResStatus.eInvalid,
          "file_log_error_tag 未初始化返回 eInvalid")
    check(log().to_sink("guard.log").tag("tag").debug("hello") == ResStatus.eInvalid,
          "file_log_debug_tag 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eInfo).disable() == ResStatus.eInvalid,
          "off_console_log_info 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eWarn).disable() == ResStatus.eInvalid,
          "off_console_log_warn 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eError).disable() == ResStatus.eInvalid,
          "off_console_log_error 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eDebug).disable() == ResStatus.eInvalid,
          "off_console_log_debug 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eInfo).enable() == ResStatus.eInvalid,
          "on_console_log_info 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eWarn).enable() == ResStatus.eInvalid,
          "on_console_log_warn 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eError).enable() == ResStatus.eInvalid,
          "on_console_log_error 未初始化返回 eInvalid")
    check(log_mgr().console().level(Log.eDebug).enable() == ResStatus.eInvalid,
          "on_console_log_debug 未初始化返回 eInvalid")
    check(log_mgr().console().list().status == ResStatus.eInvalid,
          "logger_info_loggers 未初始化返回 eInvalid")
    check(log_mgr().console("guard_logger").get_info().status == ResStatus.eInvalid,
          "logger_info_logger 未初始化返回 eInvalid")
    check(log_mgr().all().status == ResStatus.eInvalid,
          "logger_info_all 未初始化返回 eInvalid")
    check(log_mgr().file("guard_logger").remove() == ResStatus.eInvalid,
          "file_log_delete 未初始化返回 eInvalid")

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
