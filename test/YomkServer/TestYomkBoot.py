"""
TestYomkBoot.py —— YomkAPI.boot 三段失败短路/错误码透传测试（正式全面测试）

覆盖内容：
1. boot(None)：内部隐式 init 路径，返回 0 且单例被初始化
2. 三段失败短路：before/start/after 任一非 0 立即返回原值，后续段不执行
   （-1/-2/-3 三种错误码原值透传实证，验证非 -1 错误码同样穿透）
3. 执行序：全部成功时严格 before -> start -> after

剔除与差异说明（语言机制映射，非功能缺口）：
- main 第 2-5 节所有权断言（boot 以 unique_ptr 接管 YomkBoot*、调用结束后析构、
  dtor 计数实证）剔除：python 引用计数语义无所有权接管概念，无确定性析构
- 钩子未显式返回（None）等价成功 0 为 python 既有 None<->0 映射口径；本文件
  各段显式返回注入值，与 main int 返回形态一致

说明：boot(None) 消耗门面单例的 once 语义，后续注入用例的 init 调用幂等空转，
无需进程隔离

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import sys

import YomkApi

g_failed = 0

# ---- 文件级观测变量（boot 全程主线程调用，无并发访问） ----
g_before_ret = 0   # before() 注入返回值（0 成功）
g_start_ret = 0    # start() 注入返回值
g_after_ret = 0    # after() 注入返回值
g_before_count = 0
g_start_count = 0
g_after_count = 0
g_order = []       # 三段执行序记录


def reset_globals():
    global g_before_ret, g_start_ret, g_after_ret
    global g_before_count, g_start_count, g_after_count
    g_before_ret = g_start_ret = g_after_ret = 0
    g_before_count = g_start_count = g_after_count = 0
    g_order.clear()


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


class BootRecorder(YomkApi.YomkBoot):
    """三段记录器：各段计数与执行序入表，返回值由全局开关注入"""

    def before(self):
        global g_before_count
        g_before_count += 1
        g_order.append("before")
        return g_before_ret

    def start(self):
        global g_start_count
        g_start_count += 1
        g_order.append("start")
        return g_start_ret

    def after(self):
        global g_after_count
        g_after_count += 1
        g_order.append("after")
        return g_after_ret


def main():
    global g_before_ret, g_start_ret, g_after_ret
    print("===== 1. boot(None) 隐式 init =====")
    check(YomkApi.boot() == 0, "boot(None) 返回 0（不经过三段）")
    check(YomkApi.server_instance() is not None,
          "boot(None) 内部隐式 init 路径，单例被初始化")

    print("===== 2. before 失败短路 =====")
    reset_globals()
    g_before_ret = -1
    ret = YomkApi.boot(BootRecorder())
    check(ret == -1, "before 失败返回原值 -1")
    check(g_before_count == 1, "before 执行 1 次")
    check(g_start_count == 0, "start 被短路未执行")
    check(g_after_count == 0, "after 被短路未执行")

    print("===== 3. start 失败短路 =====")
    reset_globals()
    g_start_ret = -2
    ret = YomkApi.boot(BootRecorder())
    check(ret == -2, "start 失败返回原值 -2（非 -1 错误码同样透传）")
    check(g_before_count == 1, "before 执行 1 次")
    check(g_start_count == 1, "start 执行 1 次")
    check(g_after_count == 0, "after 被短路未执行")

    print("===== 4. after 失败透传 =====")
    reset_globals()
    g_after_ret = -3
    ret = YomkApi.boot(BootRecorder())
    check(ret == -3, "after 失败返回原值 -3")
    check(g_before_count == 1 and g_start_count == 1 and g_after_count == 1,
          "三段各执行 1 次（after 为末段无短路语义）")

    print("===== 5. 全部成功执行序 =====")
    reset_globals()
    ret = YomkApi.boot(BootRecorder())
    check(ret == 0, "三段全部成功返回 0")
    check(g_order == ["before", "start", "after"],
          "执行序严格 before -> start -> after")

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          f" ({g_failed} failed)")
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
