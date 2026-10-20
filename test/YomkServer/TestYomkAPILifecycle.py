"""
TestYomkAPILifecycle.py —— YomkAPI init/shutdown 单例生命周期测试（正式全面测试）

覆盖内容：
1. 单例三承诺：二次 init(n) 返回同一实例（async_thread_count 仅首次生效）；
   shutdown 后守卫回归未初始化态；关闭后 init 再调返回 None（不支持二次初始化）
2. 内置服务：init 拉起 /YomkContext /YomkFunctionPool /YomkEventLoop
3. API 层独有守卫（已初始化态，区别于服务器层守卫）：add_service(None) 返回 -1、
   del_service("") 返回 -1
4. API 层增删往返：两参改名注册、删除、重复删除；已初始化时守卫放行至服务器层
   （不存在的服务名按服务器层契约返回 eNo）
5. 幂等：二次 shutdown 不崩溃；进程正常退出码 0 即证退出清理幂等

推迟说明（模块轮）：
- main 模块便捷 API 已初始化态行为留模块轮（Logger 便捷 API 随 Logger 模块轮在
  NotInit 文件补全量守卫）

说明：本程序验证已初始化态与关闭后态，与 TestYomkAPINotInit（未初始化态）进程
隔离；门面单例状态进程级持久，shutdown 压轴执行

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys

import YomkApi
from YomkServerPy import ResStatus, YomkService

g_failed = 0


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


class DummySrv(YomkService):
    """最小演示服务：API 层增删往返与二次初始化守卫用例载体"""

    def __init__(self, server=None):
        super().__init__(server)

    def init(self):
        return 0


def name_in(names, name):
    return name in names


def main():
    print("===== 1. init 单例承诺 =====")
    first = YomkApi.init(1)
    check(first is not None, "首次 init(1) 返回非空单例")

    second = YomkApi.init(4)
    check(second is not None and second is first,
          "二次 init(4) 返回同一实例（async_thread_count 仅首次生效）")

    inst = YomkApi.server_instance()
    check(inst is first, "server_instance() 与 init 返回一致")

    print("===== 2. 内置服务拉起 =====")
    names = YomkApi.server_instance().service_names()
    check(name_in(names, "/YomkContext"), "内置服务 /YomkContext 已拉起")
    check(name_in(names, "/YomkFunctionPool"), "内置服务 /YomkFunctionPool 已拉起")
    check(name_in(names, "/YomkEventLoop"), "内置服务 /YomkEventLoop 已拉起")
    check(name_in(names, "/YomkLogger"), "内置服务 /YomkLogger 已拉起")
    check(name_in(names, "/YomkServerInfo"), "内置服务 /YomkServerInfo 已拉起")

    print("===== 3. API 层独有守卫（已初始化态） =====")
    check(YomkApi.add_service(None) == -1,
          "add_service(None) 返回 -1（YomkService is null）")
    check(YomkApi.del_service("") == -1,
          "del_service(\"\") 返回 -1（service name is empty）")

    print("===== 4. API 层增删往返 =====")
    check(YomkApi.add_service(DummySrv(YomkApi.server_instance()), "/ApiSrv") == 0,
          "add_service 两参形式改名注册成功")
    check(YomkApi.del_service("/ApiSrv") == 0, "del_service 已注册服务返回 0")
    check(YomkApi.del_service("/ApiSrv") == -1, "重复删除已删服务返回 -1")

    # 已初始化时守卫放行至服务器层：不存在的服务名按服务器层契约返回 eNo
    resp = YomkApi.request("/Nope/x", None)
    check(resp.status == ResStatus.eNo, "request 未知服务经守卫放行至服务器层返回 eNo")

    print("===== 5. shutdown 与关闭后守卫回归 =====")
    YomkApi.shutdown()
    check(YomkApi.server_instance() is None, "shutdown 后单例置空")

    YomkApi.shutdown()
    check(True, "二次 shutdown 幂等不崩溃")

    resp = YomkApi.request("/Any/func", None)
    check(resp.status == ResStatus.eInvalid, "关闭后 request 守卫回归 eInvalid")
    check(resp.msg == "YomkServer is not init", f"关闭后契约消息回归: {resp.msg}")
    check(YomkApi.new_service(DummySrv) == -1, "关闭后 new_service 守卫回归 -1")

    check(YomkApi.init() is None, "关闭后 init 再调返回 None（once 已消耗，不支持二次初始化）")

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
