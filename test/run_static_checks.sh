#!/bin/bash
# YomkServerPy 静态代码检查运行器（与 test/run_tests.sh 成对：动态测试 + 静态检测）
# 用法:
#   ./run_static_checks.sh            默认双跑 ruff + mypy
#   ./run_static_checks.sh --ruff     仅跑 ruff
#   ./run_static_checks.sh --mypy     仅跑 mypy
#   ./run_static_checks.sh -h|--help  显示本帮助
# 行为:
#   1. ruff 档: 扫描新包 src/yomk（L13 起为唯一 Python 面），
#      --isolated 消除用户级配置干扰，规则集 E4/E7/E9/F 零白名单零告警验收
#   2. mypy 档: 检查新包（MYPYPATH=src + --namespace-packages --explicit-package-bases
#      -p yomk，PEP 420 命名空间包形态，无错误码豁免）
#   3. 任一工具检出告警 → 输出 [FAIL] 摘要并以非零码退出；全部零告警 → 输出 [PASS]
#   4. 日志落盘: test/test_logs/<时间戳>/ruff_yomk.log + mypy_yomk.log + summary.log
# 依赖: ruff、mypy（缺失时报错并给安装提示: python3 -m pip install --user ruff mypy）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

RUN_RUFF=0
RUN_MYPY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --ruff) RUN_RUFF=1 ;;
        --mypy) RUN_MYPY=1 ;;
        -h|--help) usage ;;
        *) echo "错误: 未知参数 $1"; usage ;;
    esac
    shift
done
if [ ${RUN_RUFF} -eq 0 ] && [ ${RUN_MYPY} -eq 0 ]; then
    RUN_RUFF=1
    RUN_MYPY=1
fi

if [ ${RUN_RUFF} -eq 1 ] && ! python3 -m ruff --version >/dev/null 2>&1; then
    echo "错误: 未找到 ruff，请先安装: python3 -m pip install --user ruff"
    exit 1
fi
if [ ${RUN_MYPY} -eq 1 ] && ! python3 -m mypy --version >/dev/null 2>&1; then
    echo "错误: 未找到 mypy，请先安装: python3 -m pip install --user mypy"
    exit 1
fi

FAILED=0

# 日志落盘：test_logs/<时间戳>/ 下（与 run_tests.sh 的 test_logs/tmp 同居一层）
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
echo "-- 日志目录: ${LOG_ROOT}"

# ---------- ruff：新包 src/yomk 零告警验收（L13 起唯一 Python 面） ----------
if [ ${RUN_RUFF} -eq 1 ]; then
    echo "-- ruff 开始扫描新包 src/yomk ..."
    RUFF_OUT="${LOG_ROOT}/ruff_yomk.log"
    python3 -m ruff check --isolated --select E4,E7,E9,F \
        "${REPO_DIR}/src/yomk" 2>&1 \
        | tee "${RUFF_OUT}"
    rc=${PIPESTATUS[0]}
    if [ ${rc} -ne 0 ]; then
        echo "[FAIL] ruff 检出告警（退出码 ${rc}）:"
        grep -E ":[0-9]+:[0-9]+: " "${RUFF_OUT}" | head -20
        printf "[FAIL] ruff 退出码 %s\n" "${rc}" >> "${SUMMARY}"
        FAILED=1
    else
        echo "[PASS] ruff 零告警"
        printf "[PASS] ruff 零告警\n" >> "${SUMMARY}"
    fi
fi

# ---------- mypy：新包类型检查零告警验收（PEP 420 命名空间包，无错误码豁免） ----------
if [ ${RUN_MYPY} -eq 1 ]; then
    echo "-- mypy 开始扫描新包 -p yomk ..."
    MYPY_OUT="${LOG_ROOT}/mypy_yomk.log"
    MYPYPATH="${REPO_DIR}/src" python3 -m mypy \
        --namespace-packages --explicit-package-bases --no-incremental \
        --cache-dir "${LOG_ROOT}/mypy_cache_yomk" \
        -p yomk 2>&1 \
        | tee "${MYPY_OUT}"
    rc=${PIPESTATUS[0]}
    if [ ${rc} -ne 0 ]; then
        echo "[FAIL] mypy 检出告警（退出码 ${rc}）:"
        grep -E "error:" "${MYPY_OUT}" | head -20
        printf "[FAIL] mypy 退出码 %s\n" "${rc}" >> "${SUMMARY}"
        FAILED=1
    else
        echo "[PASS] mypy 零告警"
        printf "[PASS] mypy 零告警\n" >> "${SUMMARY}"
    fi
fi

if [ ${FAILED} -ne 0 ]; then
    echo "==========================================="
    echo " 静态代码检查未通过（日志目录: ${LOG_ROOT}）"
    echo "==========================================="
    echo "YomkServerPy 静态代码检查未通过" >> "${SUMMARY}"
    exit 1
fi
echo "==========================================="
echo " 静态代码检查全部通过（日志目录: ${LOG_ROOT}）"
echo "==========================================="
echo "YomkServerPy 静态代码检查全部通过" >> "${SUMMARY}"
exit 0
