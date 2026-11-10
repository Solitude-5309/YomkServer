#!/bin/bash
# YomkServer 全量示例运行器（与 run_tests.sh 同一套运转逻辑，面向 examples/ 六个示例）
# 用法:
#   ./run_example.sh             默认模式（运行全部示例，单示例超时 300s）
#   ./run_example.sh --bin DIR   指定示例可执行目录（默认 <仓库根>/bin）
#   ./run_example.sh --timeout N 单示例超时秒数（默认 300）
#   ./run_example.sh [名字...]   只跑指定示例（如 ExampleYomkService，支持多个）
# 行为:
#   1. 逐个运行 6 个示例（示例均以 getchar() 等回车退出，此处统一 </dev/null 供其立即到 EOF 正常退出）
#   2. 每个示例在独立临时工作目录运行，避免日志落盘互相覆盖
#   3. 任一示例失败（退出码非 0 / 超时）→ 立即停止，终端输出 [FAIL] 摘要与日志路径
#   4. 日志落盘: test/test_logs/<时间戳>_example/<示例名>.log + summary.log
#   5. 退出（含失败/中断/超时）时整目录删除 test/test_logs/tmp，不留任何临时产物
# 示例程序风格约定: main() 返回 0=演示全程无异常，非 0=中途报错退出

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN_DIR="${REPO_DIR}/bin"
TIMEOUT_SECS=300

usage() {
    sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

parse_args() {
    SELECTED=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --bin)     [ $# -ge 2 ] || { echo "错误: --bin 需要目录参数"; exit 1; }
                       BIN_DIR="$2"; shift ;;
            --timeout) [ $# -ge 2 ] || { echo "错误: --timeout 需要秒数参数"; exit 1; }
                       TIMEOUT_SECS="$2"; shift ;;
            -h|--help) usage ;;
            -*)        echo "错误: 未知参数 $1"; usage ;;
            *)         SELECTED+=("$1") ;;
        esac
        shift
    done
}

# 示例清单：与 examples/ 下 CMakeLists 目标一一对应
EXAMPLES=(
    ExampleYomkContext
    ExampleYomkEventLoop
    ExampleYomkFunctionPool
    ExampleYomkLogger
    ExampleYomkServerInfo
    ExampleYomkService
)

# 选中的示例（未指定则全量；指定时先校验名字合法，防止拼错静默全跑）
build_run_list() {
    if [ ${#SELECTED[@]} -gt 0 ]; then
    RUN_LIST=()
    for want in "${SELECTED[@]}"; do
        ok=0
        for e in "${EXAMPLES[@]}"; do
            [ "${want}" = "${e}" ] && ok=1 && break
        done
        if [ ${ok} -eq 0 ]; then
            echo "错误: 未知示例 '${want}'（可选: ${EXAMPLES[*]}）"
            exit 1
        fi
        RUN_LIST+=("${want}")
    done
    else
        RUN_LIST=("${EXAMPLES[@]}")
    fi
}

# 示例临时根（固定共享目录，test_logs 已 gitignore）：启动整目录重建 + 导出注入，
# 退出整目录删除（幂等；失败/超时/中断路径经 EXIT trap 全覆盖）
TEST_TMP_ROOT="${SCRIPT_DIR}/test_logs/tmp"
ensure_test_tmp_root() {
    rm -rf "${TEST_TMP_ROOT}"
    mkdir -p "${TEST_TMP_ROOT}"
    export YOMK_TEST_TMP_ROOT="${TEST_TMP_ROOT}"
}

# EXIT/INT/TERM 兜底清理临时工作目录与整个示例临时根（幂等）
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

# 门禁检查: 目录存在且所选示例可执行齐全
precheck_bin() {
    if [ ! -d "${BIN_DIR}" ]; then
        echo "错误: 示例可执行目录不存在: ${BIN_DIR}"
        echo "请先构建: cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DCMAKE_BUILD_TYPE=Release && cmake --build ${REPO_DIR}/build -j"
        return 1
    fi
    local missing=()
    local e
    for e in "${RUN_LIST[@]}"; do
        [ -x "${BIN_DIR}/${e}" ] || missing+=("${e}")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "错误: ${BIN_DIR} 缺少以下示例可执行（共 ${#missing[@]} 个）:"
        for e in "${missing[@]}"; do echo "   - ${e}"; done
        echo "请先构建: cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DCMAKE_BUILD_TYPE=Release && cmake --build ${REPO_DIR}/build -j"
        return 1
    fi
}

main() {
    parse_args "$@"
    build_run_list
    precheck_bin || exit 1
    ensure_test_tmp_root

    local log_root="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)_example"
    mkdir -p "${log_root}"
    local summary="${log_root}/summary.log"
    printf "[MODE] bin=%s timeout=%ss\n" "${BIN_DIR}" "${TIMEOUT_SECS}" >> "${summary}"

    echo "-- 示例可执行目录: ${BIN_DIR}"
    echo "-- 日志目录:       ${log_root}"
    echo "-- 示例总数:       ${#RUN_LIST[@]}（单示例超时 ${TIMEOUT_SECS}s）"
    echo ""

    local total=${#RUN_LIST[@]}
    local passed=0
    local idx=0
    local e log rc start elapsed
    for e in "${RUN_LIST[@]}"; do
        idx=$((idx + 1))
        log="${log_root}/${e}.log"
        CURRENT_WORKDIR="$(mktemp -d -p "${TEST_TMP_ROOT}")"
        printf "[%2d/%2d] %-32s " "${idx}" "${total}" "${e}"
        start=${SECONDS}
        # </dev/null: 示例以 getchar() 等回车退出，无输入时立即 EOF 正常返回
        ( cd "${CURRENT_WORKDIR}" && timeout "${TIMEOUT_SECS}" "${BIN_DIR}/${e}" ) < /dev/null > "${log}" 2>&1
        rc=$?
        elapsed=$((SECONDS - start))
        cleanup_workdir
        if [ ${rc} -eq 0 ]; then
            echo "PASS (${elapsed}s)"
            printf "[PASS] %-32s %ds\n" "${e}" "${elapsed}" >> "${summary}"
            passed=$((passed + 1))
        else
            local reason="退出码 ${rc}"
            [ ${rc} -eq 124 ] && reason="超时(>${TIMEOUT_SECS}s)"
            echo "FAIL (${reason}, ${elapsed}s)"
            printf "[FAIL] %-32s %s\n" "${e}" "${reason}" >> "${summary}"
            echo "-------------------------------------------"
            echo "示例 ${e} 失败: ${reason}"
            echo "完整日志: ${log}"
            echo "日志末尾 30 行:"
            tail -30 "${log}"
            echo "-------------------------------------------"
            exit 1
        fi
    done

    echo ""
    echo "==========================================="
    echo " YomkServer 全部示例运行通过: ${passed}/${total}"
    echo " 总耗时: ${SECONDS}s"
    echo " 日志目录: ${log_root}"
    echo "==========================================="
    echo "YomkServer 全部示例运行通过: ${passed}/${total}" >> "${summary}"
    return 0
}

main "$@"
