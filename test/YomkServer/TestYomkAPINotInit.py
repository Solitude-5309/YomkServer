"""
TestYomkAPINotInit.py —— YomkAPI 未初始化守卫全集测试（正式全面测试）

覆盖内容：
1. version() 免初始化可调用（不依赖单例，返回非空版本号）
2. 未初始化守卫默认值：server_instance() 为 None、shutdown() 安全空转（幂等）
3. request 返回 eInvalid + 契约消息 "YomkServer is not init"；async_request 静默
   返回、回调不触发
4. 服务管理守卫：new_service/add_service/del_service 返回 -1
5. 模块便捷 API 守卫抽样：统一守卫入口 _api_request 验证（context_create/
   function_pool_register/event_loop_start 返回 eInvalid + 契约消息同根 +
   context_get 特殊形态原样返回调用方默认值）

剔除与推迟说明（语言机制映射与范围口径，非功能缺口）：
- main 第 5 节模块 API 守卫五模块全量 40+ 条（Logger 23 / EventLoop 9 /
  FunctionPool 6 / Context 12 + GET 特殊形态 / ServerInfo 4）留模块轮：全量
  守卫的模块属性决定其归属，本文件抽样仅验证统一守卫入口不触模块逻辑
- main 第 4 节"守卫拒绝后调用方 delete 无泄漏无崩溃"断言剔除：python 引用计数
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
from YomkServerPy import ResStatus, YomkService

g_failed = 0
g_async_cb_count = 0  # 未初始化 async_request 回调到达计数（应恒为 0）


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
    ver = YomkApi.version()
    check(isinstance(ver, str) and ver != "", f"version() 未初始化时返回非空版本号: {ver}")

    print("===== 2. server_instance 与未初始化 shutdown 空转 =====")
    check(YomkApi.server_instance() is None, "server_instance() 未初始化返回 None")

    YomkApi.shutdown()
    check(YomkApi.server_instance() is None, "shutdown() 未初始化空转，单例保持为空")

    print("===== 3. request/async_request 未初始化守卫 =====")
    resp = YomkApi.request("/Any/func", None)
    check(resp.status == ResStatus.eInvalid, "request 未初始化返回 eInvalid")
    check(resp.msg == "YomkServer is not init",
          f"request 未初始化契约消息: {resp.msg}")

    YomkApi.async_request("/Any/func", None, on_async_cb)
    time.sleep(0.1)
    check(g_async_cb_count == 0, "async_request 未初始化静默返回，回调不触发")

    print("===== 4. 服务管理 API 未初始化守卫 =====")
    check(YomkApi.new_service(DummySrv) == -1, "new_service 未初始化返回 -1")
    check(YomkApi.add_service(None) == -1, "add_service(None) 未初始化返回 -1")
    check(YomkApi.add_service(DummySrv()) == -1, "add_service(服务实例) 未初始化返回 -1")
    check(YomkApi.del_service("/Any") == -1, "del_service 未初始化返回 -1")

    print("===== 5. 模块便捷 API 守卫抽样（统一守卫入口验证） =====")
    resp = YomkApi.context_create("guard_key", None)
    check(resp.status == ResStatus.eInvalid, "context_create 未初始化返回 eInvalid")
    resp = YomkApi.function_pool_register("guard_func", None)
    check(resp.status == ResStatus.eInvalid,
          "function_pool_register 未初始化返回 eInvalid")
    resp = YomkApi.event_loop_start("/guard_loop")
    check(resp.status == ResStatus.eInvalid,
          "event_loop_start 未初始化返回 eInvalid")
    check(resp.msg == "YomkServer is not init", f"模块 API 守卫契约消息同根: {resp.msg}")

    sentinel = object()
    check(YomkApi.context_get("guard_key", sentinel) is sentinel,
          "context_get 未初始化原样返回调用方默认值")

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
