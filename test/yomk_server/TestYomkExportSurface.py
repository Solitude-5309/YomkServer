#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkExportSurface.py —— 新包按域导出面快照守卫（L12 起）

背景：
    yomk/ 下各域子包各自定型导出面（各 __init__.py 的 __all__）。
    后续搬移/重导出若漏导出或多导出，运行期测试未必覆盖，
    故在此冻结各域 __all__ 快照。

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

DOMAINS = ["server", "boot", "ctx", "fn_pool", "event_loop", "log", "srv_info",
           "url.fn_pool", "url.log", "url.srv_info", "url.ctx"]

# 域名 → __all__ 快照（与各域 src/yomk/<域>/__init__.py 定型清单一致）
EXPECTED = {
    "server": [
        "__version__",
        "server", "ServerBuilder", "AsyncRequestBuilder",
        "YomkServer", "YomkService",
    ],
    "boot": [
        "__version__",
        "YomkBoot",
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
    # url 常量池（yomk.url 命名空间 + 各域子包，与 msg 同构）：无 __version__（非版本化域），
    # 逐域轮次扩充（第 2 轮 fn_pool、第 3 轮 log、第 4 轮 srv_info、第 5 轮 ctx）
    "url.fn_pool": [
        "FP_ALL", "FP_CALL", "FP_NAME", "FP_NAMES", "FP_REGISTER",
        "FP_UNREGISTER",
    ],
    "url.log": [
        "LOG_ALL", "LOG_CONSOLE_GLOBAL_STATE", "LOG_CONSOLE_LOG",
        "LOG_CREATE_FILE_LOGGER", "LOG_DELETE_LOGGER", "LOG_FILE_GLOBAL_STATE",
        "LOG_FILE_LOG", "LOG_LOGGER", "LOG_LOGGERS",
        "LOG_OFF_CONSOLE_LOG_BY_LEVEL", "LOG_OFF_FILE_LOG_BY_LEVEL",
        "LOG_ON_CONSOLE_LOG_BY_LEVEL", "LOG_ON_FILE_LOG_BY_LEVEL",
        "LOG_SET_CONSOLE_PROXY", "LOG_SET_FILE_LOG_PROXY",
        "LOG_WRITE_FILE_LOG",
    ],
    "url.srv_info": [
        "SRV_ALL", "SRV_FUNCTION", "SRV_FUNCTIONS", "SRV_SERVICES",
    ],
    "url.ctx": [
        "CTX_ALL", "CTX_CREATE", "CTX_DESTROY", "CTX_GET", "CTX_GROUPS",
        "CTX_KEY", "CTX_KEYS", "CTX_SET", "CTX_SET_CHECKER",
        "CTX_SET_MONITOR", "CTX_TURN_OFF_CHECKER", "CTX_TURN_OFF_MONITOR",
        "CTX_TURN_ON_CHECKER", "CTX_TURN_ON_MONITOR",
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

        # __version__ 为版本化域专属断言；url 常量池对齐 C++ 纯头文件命名空间，无版本
        if not domain.startswith("url."):
            ver = getattr(mod, "__version__", "unknown")
            check(ver != "unknown", f"{domain}: __version__ 构建期注入（={ver}）")

    print("ALL PASSED" if g_failed == 0 else "SOME FAILED")
    return g_failed


if __name__ == "__main__":
    sys.exit(main())
