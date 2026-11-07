#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkExportSurface.py —— 源片段拆分后的导出面快照守卫

背景：
    src/ 已按职责拆为多片段，构建期由 scripts/assemble_py.py 合并为
    build/YomkServer.py 再经 Cython 编译。拆分过程若漏搬/重复符号，
    运行期测试未必覆盖，故在此冻结合并前的模块导出面白名单。

口径：
    - 快照 = `sorted(n for n in dir(YomkServer) if not n.startswith("__"))`，
      去掉双下划线项以规避解释器/Cython 版本差异，保留 `_yomk_*` 内部符号；
    - 缺失（missing）与新增（added）分别断言，便于定向排障；
    - 新增公开符号属有意变更时，须同步更新 EXPECTED 清单。

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例。
"""

import sys

import YomkServer

g_failed = 0

EXPECTED = [
    "Any", "CallFunction", "Callable", "CheckStatus", "ConsoleLogProxy",
    "ConsoleLogger", "Context", "ContextChecker", "ContextEntry", "ContextGroup",
    "ContextGroupInfo", "ContextGroupKey", "ContextKeyInfo", "ContextMonitor",
    "Enum", "Event", "EventLoop", "EventLoopInfo", "EventLoopPkg",
    "EventLoopQuery", "FileLogProxy", "FileLogger", "FuncInfo", "Function",
    "Log", "LogFile", "LoggerInfo", "LoggerQuery", "LoggerState", "Monitor",
    "ResStatus", "ServerServiceInfo", "ThreadPoolExecutor", "YomkContext",
    "YomkEventLoop", "YomkFunctionPool", "YomkLogger", "YomkResponse",
    "YomkServer", "YomkServerInfo", "YomkService", "_yomk_err_pos_log",
    "_yomk_loc_base_name", "_yomk_loc_show_file", "_yomk_loc_show_func",
    "_yomk_loc_show_line", "_yomk_log_local_time_formatted",
    "_yomk_source_loc_prefix", "_yomk_weak_discard_result", "log", "logging",
    "os", "queue", "rwlock", "sys", "threading", "time", "weakref",
]


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def main():
    actual = sorted(n for n in dir(YomkServer) if not n.startswith("__"))

    missing = [n for n in EXPECTED if n not in actual]
    added = [n for n in actual if n not in EXPECTED]
    check(not missing, f"导出面无缺失（missing={missing}）")
    check(not added, f"导出面无新增（added={added}）")
    check(len(actual) == len(EXPECTED),
          f"导出面数量一致（expected={len(EXPECTED)}, actual={len(actual)}）")

    print("ALL PASSED" if g_failed == 0 else "SOME FAILED")
    return g_failed


if __name__ == "__main__":
    sys.exit(main())