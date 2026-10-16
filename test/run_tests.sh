#!/bin/bash
# YomkServerPy 一键测试脚本
# 用法: ./test/run_tests.sh [--prefix DIR] [--timeout N]
#   --prefix DIR   安装路径（缺省取 $YOMK_PREFIX_PATH，再缺省 /opt/yomk）
#   --timeout N    单测试超时秒数（默认 300）
# 行为:
#   - 自动探测安装产物 lib/pythonX.Y/site-packages 并设置 PYTHONPATH
#   - 依次运行 test/Test*.py，失败即停
#   - 日志落盘: test/test_logs/<时间戳>/（每个测试一份日志 + summary.log 汇总）
#   - 每个测试由 timeout 包裹，防卡死

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="${SCRIPT_DIR}"
DEFAULT_INSTALL_PREFIX="/opt/yomk"
DEFAULT_TIMEOUT=300

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 解析参数 ----------------
PREFIX="${YOMK_PREFIX_PATH:-${DEFAULT_INSTALL_PREFIX}}"
TIMEOUT="${DEFAULT_TIMEOUT}"
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)
            [ $# -ge 2 ] || fail "--prefix 需要目录参数"
            PREFIX="$2"; shift 2 ;;
        --timeout)
            [ $# -ge 2 ] || fail "--timeout 需要秒数参数"
            TIMEOUT="$2"; shift 2 ;;
        *)
            fail "未知参数: $1（用法: ./test/run_tests.sh [--prefix DIR] [--timeout N]）" ;;
    esac
done

# 展开 ~ 为 $HOME
PREFIX="${PREFIX/#\~/$HOME}"
# -------------el 1. 解析参数 ----------------

# -------------sl 2. 探测安装产物并设置 PYTHONPATH ----------------
PY_SITE_DIR=$(ls -d "${PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
[ -n "${PY_SITE_DIR}" ] \
    || fail "未找到 ${PREFIX}/lib/pythonX.Y/site-packages，请确认已安装（可用 --prefix 指定安装路径）"

for f in YomkServerPy.so YomkApi.py; do
    [ -f "${PY_SITE_DIR}/${f}" ] || fail "未找到 ${PY_SITE_DIR}/${f}，请先完成 cmake install"
done

export PYTHONPATH="${PY_SITE_DIR}:${PYTHONPATH}"
echo "-- PYTHONPATH: ${PY_SITE_DIR}"
# -------------el 2. 探测安装产物并设置 PYTHONPATH ----------------

# -------------sl 3. 收集测试 ----------------
TESTS=("${TEST_DIR}"/Test*.py)
[ -e "${TESTS[0]}" ] || fail "未找到测试: ${TEST_DIR}/Test*.py"
# -------------el 3. 收集测试 ----------------

# -------------sl 4. 运行测试（失败即停） ----------------
LOG_ROOT="${TEST_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"

TOTAL=${#TESTS[@]}
PASS=0
FAIL=0
echo "-- 开始运行测试: 共 ${TOTAL} 个，单测试超时 ${TIMEOUT}s"

for t in "${TESTS[@]}"; do
    name=$(basename "${t}")
    log="${LOG_ROOT}/${name%.py}.log"
    echo "-- 运行 ${name} ..."
    # stdin 喂入换行：兼容演示型测试中的 input() 人工暂停交互（无人值守时自动继续）
    if printf '\n\n' | timeout "${TIMEOUT}" python3 "${t}" >"${log}" 2>&1; then
        echo "   [PASS] ${name}"
        PASS=$((PASS+1))
    else
        rc=$?
        FAIL=$((FAIL+1))
        echo "   [FAIL] ${name} (exit=${rc})，最近输出:"
        tail -n 40 "${log}"
        break
    fi
done

{
    echo "PYTHONPATH: ${PY_SITE_DIR}"
    echo "总计: ${TOTAL}  PASS: ${PASS}  FAIL: ${FAIL}"
    echo "说明: 失败即停，未运行的测试不计入"
} >"${SUMMARY}"
# -------------el 4. 运行测试（失败即停） ----------------

# -------------sl 5. 结果摘要 ----------------
if [ "${FAIL}" -ne 0 ]; then
    echo "==========================================="
    echo " 测试失败: ${FAIL}/${TOTAL}（失败即停，未运行的测试不计入）"
    echo " 日志目录: ${LOG_ROOT}"
    echo "==========================================="
    exit 1
fi
echo "==========================================="
echo " 全部通过: ${PASS}/${TOTAL}"
echo " 日志目录: ${LOG_ROOT}"
echo "==========================================="
# -------------el 5. 结果摘要 ----------------
