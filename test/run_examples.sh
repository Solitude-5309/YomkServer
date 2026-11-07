#!/bin/bash
# YomkServerPy 示例冒烟门禁
# 用法:
#   ./test/run_examples.sh              默认超时 300s/例
#   ./test/run_examples.sh --prefix DIR 测试库产物目录（缺省 <仓库根>/build/prefix）
#   ./test/run_examples.sh --timeout N  单示例超时秒数（默认 300）
#   ./test/run_examples.sh -h|--help    打印本说明
# 口径:
#   1. 示例不随库安装（源码树直接运行，CMakeLists §10 成文），存在性校验无物可查，
#      故本闸为**运行级冒烟**：六例逐一经 PYTHONPATH=build/prefix
#      在独立临时目录全量跑通（stdin=/dev/null，示例末尾 input() 有 isatty 守卫）
#   2. 示例是各文档"完整示例"节指向的用户第一入口，运行时面（辅助函数展开、import
#      接线、非交互退出）断代即视为回归，失败即停
#   3. 日志落盘: test/test_logs/<时间戳>_examples/<示例名>.log + summary.log
#   4. 与单元回归（run_tests.sh，28 项）分闸：本脚本不并入 TESTS 数组，
#      验收口径 = 28/28 且 6/6
#   5. 范围口径：本闸只保运行时正确 + 标签链式化终态不回退

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX=""
TIMEOUT_SECS=300

usage() {
    sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 解析参数 ----------------
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)  [ $# -ge 2 ] || fail "--prefix 需要目录参数"
                   PREFIX="$2"; shift ;;
        --timeout) [ $# -ge 2 ] || fail "--timeout 需要秒数参数"
                   TIMEOUT_SECS="$2"; shift ;;
        -h|--help) usage ;;
        *) fail "未知参数: $1（用法: ./test/run_examples.sh [--prefix DIR] [--timeout N]）" ;;
    esac
    shift
done
# -------------el 1. 解析参数 ----------------

# -------------sl 2. 产物目录并设置 PYTHONPATH（与 run_tests.sh 同口径，不回退系统安装）----
build_hint() {
    echo "cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DCMAKE_BUILD_TYPE=Release && cmake --build ${REPO_DIR}/build -j"
}

if [ -z "${PREFIX}" ]; then
    PREFIX="${REPO_DIR}/build/prefix"
fi
PREFIX="${PREFIX/#\~/$HOME}"

PY_SITE_DIR=$(ls -d "${PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
[ -n "${PY_SITE_DIR}" ] \
    || fail "未找到 ${PREFIX}/lib/pythonX.Y/site-packages，请先构建: $(build_hint)（或用 --prefix 指定产物目录）"

for f in YomkServerPy.so YomkServer.py YomkApi.py; do
    [ -f "${PY_SITE_DIR}/${f}" ] || fail "未找到 ${PY_SITE_DIR}/${f}，请先构建: $(build_hint)"
done

export PYTHONPATH="${PY_SITE_DIR}:${PYTHONPATH}"
# -------------el 2. 探测安装产物并设置 PYTHONPATH ----------------

# -------------sl 3. 示例清单与总数守卫 ----------------
EXAMPLES=(
    ExampleYomkContext
    ExampleYomkFunctionPool
    ExampleYomkLogger
    ExampleYomkServerInfo
    ExampleYomkService
    ExampleYomkEventLoop
)
DECLARED_EXAMPLE_COUNT=6
if [ "${#EXAMPLES[@]}" -ne "${DECLARED_EXAMPLE_COUNT}" ]; then
    echo "错误: EXAMPLES 登记数 ${#EXAMPLES[@]} 与声明总数 ${DECLARED_EXAMPLE_COUNT} 不一致，新增示例须同步更新本文件声明"
    exit 1
fi
MISSING=()
for e in "${EXAMPLES[@]}"; do
    [ -f "${REPO_DIR}/examples/${e}.py" ] || MISSING+=("${e}.py")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "错误: ${REPO_DIR}/examples 缺少以下示例（共 ${#MISSING[@]} 个）:"
    for e in "${MISSING[@]}"; do echo "   - ${e}"; done
    exit 1
fi
# -------------el 3. 示例清单与总数守卫 ----------------

# 示例临时根：启动整目录重建、退出删除（示例的 logs/ 等副产物落其中，不污染仓库）
TMP_ROOT="${SCRIPT_DIR}/test_logs/tmp_examples"
rm -rf "${TMP_ROOT}"
mkdir -p "${TMP_ROOT}"
CURRENT_WORKDIR=""
cleanup_workdir() {
    [ -n "${CURRENT_WORKDIR}" ] && rm -rf "${CURRENT_WORKDIR}"
    CURRENT_WORKDIR=""
}
cleanup_all() {
    cleanup_workdir
    rm -rf "${TMP_ROOT}"
}
trap cleanup_all EXIT
trap 'echo ""; echo "被用户中断"; exit 130' INT TERM

# -------------sl 4. 逐例运行（失败即停） ----------------
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)_examples"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
printf "[MODE] prefix=%s\n" "${PREFIX}" >> "${SUMMARY}"

echo "-- PYTHONPATH: ${PY_SITE_DIR}"
echo "-- 日志目录:   ${LOG_ROOT}"
echo "-- 示例总数:   ${#EXAMPLES[@]}"
echo ""

TOTAL=${#EXAMPLES[@]}
PASS=0
IDX=0
START_TS=${SECONDS}

for e in "${EXAMPLES[@]}"; do
    IDX=$((IDX + 1))
    log="${LOG_ROOT}/${e}.log"
    CURRENT_WORKDIR="$(mktemp -d -p "${TMP_ROOT}")"
    printf "[%2d/%2d] %-28s " "${IDX}" "${TOTAL}" "${e}"
    T0=${SECONDS}
    ( cd "${CURRENT_WORKDIR}" && timeout "${TIMEOUT_SECS}" python3 "${REPO_DIR}/examples/${e}.py" ) </dev/null > "${log}" 2>&1
    RC=$?
    ELAPSED=$((SECONDS - T0))
    cleanup_workdir
    if [ ${RC} -eq 0 ]; then
        echo "PASS (${ELAPSED}s)"
        printf "[PASS] %-28s %ds\n" "${e}" "${ELAPSED}" >> "${SUMMARY}"
        PASS=$((PASS + 1))
    else
        REASON="退出码 ${RC}"
        [ ${RC} -eq 124 ] && REASON="超时(>${TIMEOUT_SECS}s)"
        echo "FAIL (${REASON}, ${ELAPSED}s)"
        printf "[FAIL] %-28s %s\n" "${e}" "${REASON}" >> "${SUMMARY}"
        echo "-------------------------------------------"
        echo "示例 ${e} 失败: ${REASON}（非守护线程滞留会把崩溃表现为超时）"
        echo "完整日志: ${log}"
        tail -30 "${log}"
        echo "-------------------------------------------"
        exit 1
    fi
done
# -------------el 4. 逐例运行（失败即停） ----------------

# -------------sl 5. 结果摘要 ----------------
TOTAL_ELAPSED=$((SECONDS - START_TS))
echo ""
echo "==========================================="
echo " YomkServerPy 示例冒烟通过: ${PASS}/${TOTAL}（总耗时 ${TOTAL_ELAPSED}s）"
echo " 日志目录: ${LOG_ROOT}"
echo "==========================================="
printf "YomkServerPy 示例冒烟通过: %s/%s (%ss)\n" "${PASS}" "${TOTAL}" "${TOTAL_ELAPSED}" >> "${SUMMARY}"
exit 0
# -------------el 5. 结果摘要 ----------------
