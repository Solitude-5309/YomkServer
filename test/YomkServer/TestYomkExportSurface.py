#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkExportSurface.py —— 新包按域导出面快照守卫（L12 起）

背景：
    yomk/ 下六个子包各自定型导出面（各 __init__.py 的 __all__）。
    后续搬移/重导出若漏导出或多导出，运行期测试未必覆盖，
    故在此冻结六域 __all__ 快照。

口径：
    - 快照 = 各域包的 sorted(__all__)（含 __version__ 与域入口函数）；
    - 缺失（missing）与新增（added）分别断言，便于定向排障；
    - 附加断言：__all__ 中每个名字均可 getattr 解析（无悬空导出），
      且各域 __version__ 为构建期注入（非源码树 "unknown"）；
    - 新增导出属有意变更时，须同步更新 EXPECTED 清单。

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例。
"""

import importlib
import sys

import yomk
import yomk.msg

g_failed = 0

DOMAINS = ["server", "ctx", "fn_pool", "event_loop", "log", "srv_info"]

# 域名 → __all__ 快照（与各域 src/yomk/<域>/__init__.py 定型清单一致）
EXPECTED = {
    "server": [
        "__version__",
        "server", "ServerBuilder", "AsyncRequestBuilder", "YomkBoot",
        "YomkServer", "YomkService",
    ],
    "ctx": [
        "__version__",
        "ctx", "ContextBuilder", "ContextValueBuilder", "GroupSwitch",
        "ContextGroup", "ContextEntry", "Monitor", "YomkContext",
    ],
    "fn_pool": [
        "__version__",
        "fn_pool", "FnPoolBuilder",
        "YomkFunctionPool",
    ],
    "event_loop": [
        "__version__",
        "event_loop", "EventLoopBuilder",
        "Event", "EventLoop", "YomkEventLoop",
    ],
    "log": [
        "__version__",
        "log", "log_mgr",
        "LogBuilder", "SinkLogBuilder", "ConsoleMgrBuilder", "FileMgrBuilder",
        "LocBuilder", "LogMgrBuilder",
        "ConsoleLogger", "FileLogger", "YomkLogger",
    ],
    "srv_info": [
        "__version__",
        "srv_info", "SrvInfoBuilder",
        "YomkServerInfo",
    ],
}


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def main():
    check(yomk.__file__ is None,
          "命名空间锚点: yomk 顶层无 __init__（__file__ is None）")
    check(yomk.msg.__file__ is None,
          "命名空间锚点: yomk.msg 层无 __init__（__file__ is None）")

    for domain in DOMAINS:
        mod = importlib.import_module(f"yomk.{domain}")
        actual = sorted(getattr(mod, "__all__", []))
        expected = sorted(EXPECTED[domain])

        missing = [n for n in expected if n not in actual]
        added = [n for n in actual if n not in expected]
        check(not missing, f"{domain}: 导出面无缺失（missing={missing}）")
        check(not added, f"{domain}: 导出面无新增（added={added}）")
        check(len(actual) == len(expected),
              f"{domain}: 导出面数量一致（expected={len(expected)}, "
              f"actual={len(actual)}）")

        dangling = [n for n in actual if not hasattr(mod, n)]
        check(not dangling, f"{domain}: __all__ 无悬空名字（dangling={dangling}）")

        ver = getattr(mod, "__version__", "unknown")
        check(ver != "unknown", f"{domain}: __version__ 构建期注入（={ver}）")

    print("ALL PASSED" if g_failed == 0 else "SOME FAILED")
    return g_failed


if __name__ == "__main__":
    sys.exit(main())
