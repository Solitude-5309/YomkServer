#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLayout.py
@brief 新包布局、类型声明与本地诊断一致性测试（L9）

覆盖内容（5 个 Section）：
S1. 命名空间骨架：src/yomk 与 src/yomk/msg 无 __init__.py/.pyi/py.typed；
    import yomk 不加载任何子模块（yomk.__file__ is None）
S2. pyi 完整性：每个 .py 有同名 .pyi；每个 .pyi 有对应源（_version.pyi 对应
    _version.py.in）；每个运行时域有 _version.py.in/_version.pyi；每个叶子子包
    有 py.typed
S3. __init__.pyi 显式 re-export：只用 from ... import X as X 形式
S4. 共享诊断：src/yomk/_diag.py(.pyi) 存在且仅 import 标准库；log 域专属
    扩展 _logSourceLoc.py(.pyi) 存在且不含 import logging / yomk.log；
    各域不再自带 _diag 副本
S5. 诊断可独立输出：共享 _diag 独立进程输出六个等级（进程内不得出现
    yomk.log）；log 域 _yomk_err_pos_log 独立输出错误位置日志

运行方式（仓库根目录）：
    PYTHONPATH=src python3 test/yomk_layout/TestYomkLayout.py
退出码 0=全部通过，非 0=存在失败用例。
"""

import ast
import os
import subprocess
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parents[2]
PKG_DIR = REPO_DIR / "src" / "yomk"
DOMAINS = ["server", "ctx", "event_loop", "fn_pool", "log", "srv_info"]
LEAVES = [PKG_DIR / d for d in DOMAINS] + [PKG_DIR / "msg" / d for d in DOMAINS]
DIAG_LEVELS = [("trace", "[TRACE]"), ("debug", "[DEBUG]"), ("info", "[INFO ]"),
               ("warn", "[WARN ]"), ("error", "[ERROR]"), ("fatal", "[FATAL]")]
BANNED_DIAG_PATTERNS = ["import logging", "import yomk.log", "from yomk.log"]

g_failed = 0


def check(cond, msg):
    global g_failed
    if cond:
        print("[ OK ] " + msg)
    else:
        g_failed += 1
        print("[FAIL] " + msg)


def walk_files(suffix):
    for path in sorted(PKG_DIR.rglob("*" + suffix)):
        if "__pycache__" not in path.parts:
            yield path


def run_python(code, timeout=30):
    env = os.environ.copy()
    existing = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = (str(REPO_DIR / "src") +
                         (os.pathsep + existing if existing else ""))
    return subprocess.run([sys.executable, "-c", code], cwd=str(REPO_DIR),
                          env=env, capture_output=True, text=True,
                          timeout=timeout)


def s1_namespace_skeleton():
    print("-- S1 命名空间骨架")
    for name in ("__init__.py", "__init__.pyi", "py.typed"):
        check(not (PKG_DIR / name).exists(), "src/yomk/%s 不存在" % name)
        check(not (PKG_DIR / "msg" / name).exists(),
              "src/yomk/msg/%s 不存在" % name)

    result = run_python(
        "import sys\n"
        "import yomk\n"
        "assert yomk.__file__ is None, 'yomk.__file__ must be None'\n"
        "loaded = sorted(m for m in sys.modules\n"
        "                if m == 'yomk' or m.startswith('yomk.'))\n"
        "assert loaded == ['yomk'], loaded\n"
        "print('NS OK')\n")
    check(result.returncode == 0 and "NS OK" in result.stdout,
          "import yomk 不加载子模块（%s）" % (result.stderr.strip() or "ok"))


def s2_pyi_and_markers():
    print("-- S2 pyi 完整性与叶子 py.typed")
    for path in walk_files(".py"):
        check(path.with_suffix(".pyi").exists(),
              "有同名 pyi: %s" % path.relative_to(REPO_DIR))
    for path in walk_files(".pyi"):
        if path.name == "_version.pyi":
            ok = path.with_suffix(".py.in").exists()
        else:
            ok = path.with_suffix(".py").exists()
        check(ok, "pyi 有对应源: %s" % path.relative_to(REPO_DIR))

    for domain in DOMAINS:
        domain_dir = PKG_DIR / domain
        check((domain_dir / "_version.py.in").exists(),
              "%s/_version.py.in 存在" % domain)
        check((domain_dir / "_version.pyi").exists(),
              "%s/_version.pyi 存在" % domain)
    for leaf in LEAVES:
        check((leaf / "py.typed").exists(),
              "叶子 py.typed: %s" % leaf.relative_to(REPO_DIR))


def s3_explicit_reexports():
    print("-- S3 __init__.pyi 显式 re-export")
    for path in walk_files("__init__.pyi"):
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        explicit = True
        for node in tree.body:
            if isinstance(node, ast.Import):
                explicit = False
            elif isinstance(node, ast.ImportFrom):
                for alias in node.names:
                    if alias.asname != alias.name:
                        explicit = False
        check(explicit, "全部 from-import 均为 X as X: %s"
              % path.relative_to(REPO_DIR))


def s4_shared_diagnostics():
    print("-- S4 共享诊断模块与 log 域专属扩展")
    shared = PKG_DIR / "_diag.py"
    shared_pyi = PKG_DIR / "_diag.pyi"
    check(shared.exists(), "src/yomk/_diag.py 存在（共享六级诊断）")
    check(shared_pyi.exists(), "src/yomk/_diag.pyi 存在")
    text = shared.read_text(encoding="utf-8")
    for pattern in BANNED_DIAG_PATTERNS:
        check(pattern not in text, "yomk/_diag.py 不含 %r" % pattern)
    loc = PKG_DIR / "log" / "_logSourceLoc.py"
    loc_pyi = PKG_DIR / "log" / "_logSourceLoc.pyi"
    check(loc.exists(), "log/_logSourceLoc.py 存在（log 域专属扩展）")
    check(loc_pyi.exists(), "log/_logSourceLoc.pyi 存在")
    ltext = loc.read_text(encoding="utf-8")
    for pattern in BANNED_DIAG_PATTERNS:
        check(pattern not in ltext, "log/_logSourceLoc.py 不含 %r" % pattern)
    for domain in DOMAINS:
        check(not (PKG_DIR / domain / "_diag.py").exists(),
              "%s/_diag.py 不再自带副本（共享 yomk._diag）" % domain)


def s5_diagnostics_standalone():
    print("-- S5 诊断独立输出（独立进程）")
    code = (
        "import sys\n"
        "import yomk._diag as d\n"
        "assert 'yomk.log' not in sys.modules, "
        "'yomk.log must not be imported by yomk._diag'\n"
        "for level in (%s):\n"
        "    getattr(d, level)('layout probe')\n"
        "print('DIAG OK')\n"
        % ", ".join(repr(name) for name, _ in DIAG_LEVELS))
    result = run_python(code)
    tags_ok = all(tag in result.stdout for _, tag in DIAG_LEVELS)
    check(result.returncode == 0 and "DIAG OK" in result.stdout and tags_ok,
          "共享 yomk._diag 六等级直接输出（%s）"
          % (result.stderr.strip() or "ok"))
    code2 = (
        "import yomk.log._logSourceLoc as d\n"
        "d._yomk_err_pos_log('layout probe')\n"
        "print('LOC OK')\n")
    result2 = run_python(code2)
    check(result2.returncode == 0 and "LOC OK" in result2.stdout
          and "[ERROR] [Yomk]" in result2.stdout,
          "log._logSourceLoc._yomk_err_pos_log 独立输出（%s）"
          % (result2.stderr.strip() or "ok"))


def main():
    s1_namespace_skeleton()
    s2_pyi_and_markers()
    s3_explicit_reexports()
    s4_shared_diagnostics()
    s5_diagnostics_standalone()

    print(("ALL PASSED" if g_failed == 0 else "SOME FAILED") +
          " (%d failed)" % g_failed)
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())