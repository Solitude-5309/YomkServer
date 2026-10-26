#!/bin/bash
# YomkServerPy 全量测试运行器
# 用法:
#   ./test/run_tests.sh              快速模式（压测规模缩为 10000）
#   ./test/run_tests.sh --full       完整规模（测试内部默认，10 万级投递）
#   ./test/run_tests.sh --prefix DIR 测试库产物目录（缺省 <仓库根>/build/prefix）
#   ./test/run_tests.sh --timeout N  单测试超时秒数（默认 300，--full 默认 900）
#   ./test/run_tests.sh -h|--help    打印本说明
# 测试库产物目录（对齐 main run_tests.sh 只消费仓库内本地产物的设计，无系统安装回退）:
#   默认 <仓库根>/build/prefix；--prefix DIR 可显式指定其他产物目录
#   产物缺失时报错并给出构建安装命令，不会静默回退系统安装（避免多份产物不同步时跑旧库）
# 行为:
#   1. 自动探测 lib/pythonX.Y/site-packages 并设置 PYTHONPATH
#   2. 逐个运行 TESTS 清单（快->慢，stress 类压尾），测试文件按模块子目录分类
#      （TESTS 项带模块前缀），每个测试在独立临时工作目录运行
#   3. 任一测试失败（退出码非 0 / 超时）-> 立即停止，输出 [FAIL] 行摘要与日志路径
#   4. 日志落盘: test/test_logs/<时间戳>/<测试名>.log + summary.log
#   5. 测试临时产物统一落 test/test_logs/tmp（经 YOMK_TEST_TMP_ROOT 注入测试进程，
#      测试内取根、独立运行回退系统临时目录）；启动时整目录重建（兜底上次崩溃遗留），
#      退出（含失败/中断/超时）时整目录删除，不留任何临时产物
# 测试程序风格约定: 退出码 0=全部通过，非 0=存在失败用例（[FAIL] 行输出）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 测试文件定位：TESTS 项带模块子目录前缀（YomkServer/TestYomkPkg 形态），
# 完整路径 = test/<TESTS 项>.py
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX=""
TIMEOUT_SECS=300
FULL_MODE=0
USER_TIMEOUT=0

usage() {
    sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 解析参数 ----------------
while [ $# -gt 0 ]; do
    case "$1" in
        --full)    FULL_MODE=1 ;;
        --prefix)  [ $# -ge 2 ] || fail "--prefix 需要目录参数"
                   PREFIX="$2"; shift ;;
        --timeout) [ $# -ge 2 ] || fail "--timeout 需要秒数参数"
                   TIMEOUT_SECS="$2"; USER_TIMEOUT=1; shift ;;
        -h|--help) usage ;;
        *) fail "未知参数: $1（用法: ./test/run_tests.sh [--full] [--prefix DIR] [--timeout N]）" ;;
    esac
    shift
done

# --full 未显式指定超时时放宽到 900s
if [ "${FULL_MODE}" -eq 1 ] && [ "${USER_TIMEOUT}" -eq 0 ]; then
    TIMEOUT_SECS=900
fi
# -------------el 1. 解析参数 ----------------

# 测试临时根（固定共享目录，test_logs 已 gitignore）：启动整目录重建 + 导出注入，
# 退出整目录删除（幂等；失败/超时/中断路径经 EXIT trap 全覆盖）
TEST_TMP_ROOT="${SCRIPT_DIR}/test_logs/tmp"
ensure_test_tmp_root() {
    rm -rf "${TEST_TMP_ROOT}"
    mkdir -p "${TEST_TMP_ROOT}"
    export YOMK_TEST_TMP_ROOT="${TEST_TMP_ROOT}"
}

# EXIT/INT/TERM 兜底清理临时工作目录与整个测试临时根（幂等）
CURRENT_WORKDIR=""
cleanup_workdir() {
    [ -n "${CURRENT_WORKDIR}" ] && rm -rf "${CURRENT_WORKDIR}"
    CURRENT_WORKDIR=""
}
cleanup_all() {
    cleanup_workdir
    rm -rf "${TEST_TMP_ROOT}"
}
trap cleanup_all EXIT
trap 'echo ""; echo "被用户中断"; exit 130' INT TERM

# 启动即整目录重建测试临时根（兜底上次崩溃遗留），并注入测试进程
ensure_test_tmp_root

# -------------sl 2. 测试库产物目录并设置 PYTHONPATH ----------------
# 对齐 main run_tests.sh 设计：只消费仓库内本地产物（<仓库根>/build/prefix，--prefix 可覆盖），
# 不回退系统安装（YOMK_PREFIX_PATH / /opt/yomk）——避免多份产物不同步时测试静默跑旧库；
# 产物缺失时报错并给出构建命令（对齐 main 的 build_hint 门禁；
# 构建期直接产出测试产物到 build/prefix，与 main 一致不含 install 步骤）
build_hint() {
    echo "cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DCMAKE_BUILD_TYPE=Release && cmake --build ${REPO_DIR}/build -j"
}

# 默认固定仓库本地产物目录（--prefix 可覆盖）
if [ -z "${PREFIX}" ]; then
    PREFIX="${REPO_DIR}/build/prefix"
fi
PREFIX="${PREFIX/#\~/$HOME}"

PY_SITE_DIR=$(ls -d "${PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
[ -n "${PY_SITE_DIR}" ] \
    || fail "未找到 ${PREFIX}/lib/pythonX.Y/site-packages，请先构建: $(build_hint)（或用 --prefix 指定产物目录）"

for f in YomkServerPy.so YomkApi.py; do
    [ -f "${PY_SITE_DIR}/${f}" ] || fail "未找到 ${PY_SITE_DIR}/${f}，请先构建: $(build_hint)"
done

export PYTHONPATH="${PY_SITE_DIR}:${PYTHONPATH}"
# -------------el 2. 探测安装产物并设置 PYTHONPATH ----------------

# 测试清单：按模块分组、快->慢，stress 类压尾（项带模块子目录前缀，test/<模块>/<名>.py；
# 新增测试在此登记；模块轮测试追加对应模块分组与条目）
TESTS=(
    # YomkServer 模块（基础服务框架）
    YomkServer/TestYomkPkg
    YomkServer/TestYomkService
    YomkServer/TestYomkServer
    YomkServer/TestYomkAPINotInit
    YomkServer/TestYomkAPILifecycle
    YomkServer/TestYomkBoot
    YomkServer/TestYomkShutdownSeq
    YomkServer/TestYomkWeakFunc
    YomkServer/TestYomkConcurrency
    # YomkContext 模块
    YomkContext/TestYomkContextCRUD
    YomkContext/TestYomkContextCheckerMonitor
    YomkContext/TestYomkContextLifecycle
    YomkContext/TestYomkContextConcurrency
    # YomkFunctionPool 模块
    YomkFunctionPool/TestYomkFunctionPoolLifecycle
    YomkFunctionPool/TestYomkFunctionPoolConcurrency
    YomkFunctionPool/TestYomkFunctionPoolStress
    # YomkServerInfo 模块
    YomkServerInfo/TestYomkServerInfoLifecycle
    # YomkEventLoop 模块
    YomkEventLoop/TestYomkEventLoopLifecycle
    YomkEventLoop/TestYomkEventLoopConcurrency
    YomkEventLoop/TestYomkEventLoopStress
    # YomkLogger 模块
    YomkLogger/TestYomkLoggerDirect
    YomkLogger/TestYomkLoggerLifecycle
    YomkLogger/TestYomkLoggerConcurrency
    YomkLogger/TestYomkLoggerStress
)

# -------------sl 3. 门禁检查 ----------------
MISSING=()
for t in "${TESTS[@]}"; do
    [ -f "${SCRIPT_DIR}/${t}.py" ] || MISSING+=("${t}.py")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "错误: ${SCRIPT_DIR} 缺少以下测试文件（共 ${#MISSING[@]} 个）:"
    for t in "${MISSING[@]}"; do echo "   - ${t}"; done
    exit 1
fi
# -------------el 3. 门禁检查 ----------------

# -------------sl 4. 压测规模 ----------------
if [ "${FULL_MODE}" -eq 1 ]; then
    unset YOMK_TEST_STRESS_SCALE
    echo "-- 模式: 完整规模（--full），单测试超时 ${TIMEOUT_SECS}s"
else
    export YOMK_TEST_STRESS_SCALE=10000
    echo "-- 模式: 快速（YOMK_TEST_STRESS_SCALE=10000，--full 可切完整规模），单测试超时 ${TIMEOUT_SECS}s"
fi
# -------------el 4. 压测规模 ----------------

# -------------sl 5. 运行测试（失败即停） ----------------
# 日志根保持在脚本目录（test/）下
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
printf "[MODE] prefix=%s scale=%s\n" "${PREFIX}" "${YOMK_TEST_STRESS_SCALE:-full}" >> "${SUMMARY}"

echo "-- PYTHONPATH: ${PY_SITE_DIR}"
echo "-- 日志目录:   ${LOG_ROOT}"
echo "-- 测试总数:   ${#TESTS[@]}"
echo ""

TOTAL=${#TESTS[@]}
PASS=0
IDX=0
START_TS=${SECONDS}

for t in "${TESTS[@]}"; do
    IDX=$((IDX + 1))
    # 显示名/日志名取纯测试名，路径按模块前缀定位
    T_NAME="${t##*/}"
    log="${LOG_ROOT}/${T_NAME}.log"
    CURRENT_WORKDIR="$(mktemp -d -p "${TEST_TMP_ROOT}")"
    printf "[%2d/%2d] %-32s " "${IDX}" "${TOTAL}" "${T_NAME}"
    T0=${SECONDS}
    # stdin 喂入换行：兼容演示型测试中的 input() 人工暂停交互（无人值守时自动继续）
    ( cd "${CURRENT_WORKDIR}" && printf '\n\n' | timeout "${TIMEOUT_SECS}" python3 "${SCRIPT_DIR}/${t}.py" ) > "${log}" 2>&1
    RC=$?
    ELAPSED=$((SECONDS - T0))
    cleanup_workdir
    if [ ${RC} -eq 0 ]; then
        echo "PASS (${ELAPSED}s)"
        printf "[PASS] %-32s %ds\n" "${T_NAME}" "${ELAPSED}" >> "${SUMMARY}"
        PASS=$((PASS + 1))
    else
        REASON="退出码 ${RC}"
        [ ${RC} -eq 124 ] && REASON="超时(>${TIMEOUT_SECS}s)"
        echo "FAIL (${REASON}, ${ELAPSED}s)"
        printf "[FAIL] %-32s %s\n" "${T_NAME}" "${REASON}" >> "${SUMMARY}"
        echo "-------------------------------------------"
        echo "测试 ${t} 失败: ${REASON}"
        echo "完整日志: ${log}"
        FAIL_COUNT=$(grep -c "\[FAIL\]" "${log}" 2>/dev/null || true)
        if [ -n "${FAIL_COUNT}" ] && [ "${FAIL_COUNT}" -gt 0 ]; then
            echo "失败用例（共 ${FAIL_COUNT} 行 [FAIL]，摘要如下）:"
            grep "\[FAIL\]" "${log}" | head -20
        else
            echo "（无 [FAIL] 行，可能为崩溃/超时，请查看完整日志）"
            tail -30 "${log}"
        fi
        echo "-------------------------------------------"
        exit 1
    fi
done
# -------------el 5. 运行测试（失败即停） ----------------

# -------------sl 6. 结果摘要 ----------------
TOTAL_ELAPSED=$((SECONDS - START_TS))
echo "YomkServerPy 全量测试通过: ${PASS}/${TOTAL}" >> "${SUMMARY}"

echo ""
echo "==========================================="
echo " YomkServerPy 全量测试通过: ${PASS}/${TOTAL}"
echo " 总耗时: ${TOTAL_ELAPSED}s"
echo " 日志目录: ${LOG_ROOT}"
echo "==========================================="
exit 0
# -------------el 6. 结果摘要 ----------------
