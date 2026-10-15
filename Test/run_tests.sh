#!/bin/bash
# YomkServer 全量测试运行器
# 用法:
#   ./run_tests.sh              快速模式（EventLoop 压测规模缩为 10000）
#   ./run_tests.sh --full       完整规模（测试内部默认，10 万级投递）
#   ./run_tests.sh --bin DIR    指定测试可执行目录（默认 <仓库根>/bin）
#   ./run_tests.sh --timeout N  单测试超时秒数（默认 300，--full 默认 900）
#   ./run_tests.sh --sanitizer <套件列表>   插桩全量回归，逗号分隔多套顺序执行:
#       ./run_tests.sh --sanitizer asan        仅 ASan+UBSan 一轮
#       ./run_tests.sh --sanitizer asan,tsan   asan、tsan 各一轮（顺序执行，避免并发 CPU 争抢假失败）
#       套件: asan → ASan+UBSan+LSan 插桩，自动注入 ASAN/UBSAN_OPTIONS 与
#                    LSAN_OPTIONS=suppressions（契约：suppressions 必须走 LSAN_OPTIONS，
#                    走 ASAN_OPTIONS 在本机 GCC libasan 上对任何非空文件均 parse 失败）；
#            tsan → ThreadSanitizer 插桩，自动以 setarch -R 禁用 ASLR 包裹运行
#                    （契约：本机内核 high-entropy ASLR 下 TSan runtime 启动即
#                    "unexpected memory mapping"，退出码 66）；默认 off 行为不变
# 行为:
#   1. 运行前清理 /tmp/yomk_logger_* 残留（上次崩溃遗留）
#   2. 逐个运行 26 个测试（快→慢，stress 类压尾），每个测试在独立临时工作目录运行
#   3. 任一测试失败（退出码非 0 / 超时）→ 立即停止，终端输出 [FAIL] 行摘要与日志路径
#   4. 日志落盘: Test/test_logs/<时间戳>/<测试名>.log + summary.log
#   5. 全部结束后复查现场残留，发现则清理并计为失败
# 测试程序风格约定: main() 返回 0=全部通过，非 0=存在失败用例（[FAIL] 行输出）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN_DIR="${REPO_DIR}/bin"
TIMEOUT_SECS=300
FULL_MODE=0
USER_TIMEOUT=0
SANITIZER="off"
# sanitizer 运行包裹器（tsan 模式为 setarch 禁 ASLR，其余为空）
SAN_RUNNER=()
# 本轮执行的 sanitizer 轮次清单（--sanitizer 逗号分隔/重复累积；空 = 单轮默认 off）
SAN_LIST=()

usage() {
    sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

# 解析 --sanitizer 值到 SAN_LIST: 支持逗号分隔多套（asan,tsan）与选项重复累积（--sanitizer asan --sanitizer tsan）
add_sanitizer_list() {
    local raw="$1" item
    local items=()
    IFS=',' read -ra items <<< "${raw}"
    for item in "${items[@]}"; do
        case "${item}" in
            asan|tsan) SAN_LIST+=("${item}") ;;
            off) ;;  # off 即跳过该轮；全部为 off 时由 parse_args 兜底为默认单轮
            "") ;;   # 容忍尾逗号/空段
            *) echo "错误: 未知 sanitizer '${item}'（可选 off/asan/tsan，逗号分隔多套）"; exit 1 ;;
        esac
    done
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --full)    FULL_MODE=1 ;;
            --bin)     [ $# -ge 2 ] || { echo "错误: --bin 需要目录参数"; exit 1; }
                       BIN_DIR="$2"; shift ;;
            --timeout) [ $# -ge 2 ] || { echo "错误: --timeout 需要秒数参数"; exit 1; }
                       TIMEOUT_SECS="$2"; USER_TIMEOUT=1; shift ;;
            --sanitizer) [ $# -ge 2 ] || { echo "错误: --sanitizer 需要参数（off/asan/tsan，逗号分隔多套）"; exit 1; }
                       add_sanitizer_list "$2"
                       shift ;;
            -h|--help) usage ;;
            *) echo "错误: 未知参数 $1"; usage ;;
        esac
        shift
    done
    # sanitizer 清单兜底: 未指定/全部为 off → 单轮默认模式（现有行为零变化）
    if [ ${#SAN_LIST[@]} -eq 0 ]; then
        SAN_LIST=("off")
    fi
    # --full 未显式指定超时时放宽到 900s
    if [ "${FULL_MODE}" -eq 1 ] && [ "${USER_TIMEOUT}" -eq 0 ]; then
        TIMEOUT_SECS=900
    fi
}

# EXIT/INT/TERM 兜底清理临时工作目录（幂等）
CURRENT_WORKDIR=""
cleanup_workdir() {
    [ -n "${CURRENT_WORKDIR}" ] && rm -rf "${CURRENT_WORKDIR}"
    CURRENT_WORKDIR=""
}
trap cleanup_workdir EXIT
trap 'echo ""; echo "被用户中断"; exit 130' INT TERM

# 测试清单：按模块分组、快→慢，stress 类压尾（与 Test/ 下 CMakeLists 目标一一对应）
TESTS=(
    # YomkServer 模块
    TestYomkSimpleThreadPool
    TestYomkWeakFunc
    TestYomkAPINotInit
    TestYomkAPILifecycle
    TestYomkBoot
    TestYomkServer
    TestYomkService
    TestYomkShutdownSeq
    TestYomkConcurrency
    TestYomkPkg
    TestYomkIntegration
    # ServerInfo 模块
    TestYomkServerInfoLifecycle
    # FunctionPool 模块
    TestYomkFunctionPoolLifecycle
    TestYomkFunctionPoolConcurrency
    TestYomkFunctionPoolStress
    # Context 模块
    TestYomkContextCRUD
    TestYomkContextCheckerMonitor
    TestYomkContextLifecycle
    TestYomkContextConcurrency
    # EventLoop 模块
    TestYomkEventLoopLifecycle
    TestYomkEventLoopConcurrency
    TestYomkEventLoopStress
    # Logger 模块（含大量文件 IO 的 stress，压尾）
    TestYomkLoggerLifecycle
    TestYomkLoggerDirect
    TestYomkLoggerConcurrency
    TestYomkLoggerStress
)

# 构建提示按 sanitizer 模式生成（sanitizer 使用独立插桩构建目录，输出目录隔离避免覆盖根 bin/）
build_hint() {
    if [ "${SANITIZER}" = "off" ]; then
        echo "cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DYOMK_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release && cmake --build ${REPO_DIR}/build -j"
    else
        local outdir="${REPO_DIR}/build-${SANITIZER}/bin"
        echo "cmake -S ${REPO_DIR} -B ${REPO_DIR}/build-${SANITIZER} -DYOMK_BUILD_TESTS=ON -DYOMK_TEST_SANITIZER=${SANITIZER} -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${outdir} -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${outdir} -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${outdir} && cmake --build ${REPO_DIR}/build-${SANITIZER} -j"
    fi
}

# 静默检查: 目录存在且 26 个测试可执行齐全（不打印任何输出，供自动构建决策）
check_bin_ready() {
    [ -d "${BIN_DIR}" ] || return 1
    local t
    for t in "${TESTS[@]}"; do
        [ -x "${BIN_DIR}/${t}" ] || return 1
    done
    return 0
}

# 门禁检查: 不就绪时打印详细缺失（默认模式缺失属真失败；sanitizer 模式由调用方先行自愈）
precheck_bin() {
    if check_bin_ready; then
        return 0
    fi
    if [ ! -d "${BIN_DIR}" ]; then
        echo "错误: 测试可执行目录不存在: ${BIN_DIR}"
        echo "请先构建: $(build_hint)"
        return 1
    fi
    local missing=()
    local t
    for t in "${TESTS[@]}"; do
        [ -x "${BIN_DIR}/${t}" ] || missing+=("${t}")
    done
    echo "错误: ${BIN_DIR} 缺少以下测试可执行（共 ${#missing[@]} 个）:"
    for t in "${missing[@]}"; do echo "   - ${t}"; done
    echo "请先构建: $(build_hint)"
    return 1
}

# sanitizer 模式独有: 缺失即自动配置+构建插桩版本（失败报手动命令）
# （构建输出实时透传控制台——进度百分比逐行可见，不落盘日志文件）
ensure_sanitizer_build() {
    local bdir="${REPO_DIR}/build-${SANITIZER}"
    local outdir="${bdir}/bin"
    echo "-- [sanitizer=${SANITIZER}] 测试可执行缺失，自动构建 ${bdir}（插桩全新全量编译需数分钟，进度逐行输出）..."
    local start=${SECONDS}
    cmake -S "${REPO_DIR}" -B "${bdir}" -DYOMK_BUILD_TESTS=ON -DYOMK_TEST_SANITIZER="${SANITIZER}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${outdir}" \
        -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="${outdir}" \
        -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="${outdir}" \
        || { echo "错误: cmake 配置失败，请手动执行: $(build_hint)"; exit 1; }
    cmake --build "${bdir}" -j"$(nproc)" \
        || { echo "错误: 构建失败，请手动执行: $(build_hint)"; exit 1; }
    echo "-- [sanitizer=${SANITIZER}] 自动构建完成（耗时 $((SECONDS - start))s）"
}

# 运行前清理上次运行可能遗留的测试产物（测试中途崩溃时自清理不会执行）
clean_residue() {
    shopt -s nullglob
    local residue=(/tmp/yomk_logger_*)
    shopt -u nullglob
    if [ ${#residue[@]} -gt 0 ]; then
        echo "-- 清理上次运行残留的测试产物: ${#residue[@]} 个 /tmp/yomk_logger_* 目录"
        rm -rf "${residue[@]}"
    fi
}

# 全部结束后复查现场：发现残留则清理并计为失败
check_residue() {
    shopt -s nullglob
    local residue=(/tmp/yomk_logger_*)
    shopt -u nullglob
    if [ ${#residue[@]} -gt 0 ]; then
        echo "[FAIL] 测试结束后发现现场残留（${#residue[@]} 个）:"
        for p in "${residue[@]}"; do echo "   - ${p}"; done
        rm -rf "${residue[@]}"
        echo "       已清理，但对应测试的清理逻辑未在正常路径覆盖，请排查"
        return 1
    fi
    return 0
}

main() {
    parse_args "$@"
    clean_residue

    # 多套 sanitizer 顺序逐轮执行（并发会因 CPU 争抢造成时序类断言假失败）
    local san round_total=${#SAN_LIST[@]} round_idx=0
    for san in "${SAN_LIST[@]}"; do
        SANITIZER="${san}"
        round_idx=$((round_idx + 1))
        if [ "${round_total}" -gt 1 ]; then
            echo ""
            echo "================ 第 ${round_idx}/${round_total} 轮: sanitizer=${san} ================"
        fi
        run_one_suite || exit 1
    done
    if [ "${round_total}" -gt 1 ]; then
        echo ""
        echo "==========================================="
        echo " 全部 ${round_total} 轮 sanitizer 回归通过"
        echo "==========================================="
    fi
}

# 单轮完整流程: 定位/构建插桩目录 → 环境契约注入 → 26 测试循环 → 残留验收
run_one_suite() {
    # sanitizer 模式: 默认指向独立插桩构建目录（用户显式 --bin 时尊重其值）
    if [ "${SANITIZER}" != "off" ] && [ "${BIN_DIR}" = "${REPO_DIR}/bin" ]; then
        BIN_DIR="${REPO_DIR}/build-${SANITIZER}/bin"
    fi
    # 缺失处理: sanitizer 模式属预期自愈场景，静默检测后直接自动构建（不报错误）；
    # 默认模式缺失才是真失败，报详细错误并退出。构建后统一 precheck_bin 门禁
    if ! check_bin_ready; then
        if [ "${SANITIZER}" = "off" ]; then
            precheck_bin || return 1
        fi
        ensure_sanitizer_build
        precheck_bin || return 1
    fi

    # 压测规模: 快速模式缩小 EventLoop 压测规模；--full 用测试内部默认
    if [ "${FULL_MODE}" -eq 1 ]; then
        unset YOMK_TEST_STRESS_SCALE
        echo "-- 模式: 完整规模（--full），单测试超时 ${TIMEOUT_SECS}s"
    else
        export YOMK_TEST_STRESS_SCALE=10000
        echo "-- 模式: 快速（YOMK_TEST_STRESS_SCALE=10000，--full 可切完整规模），单测试超时 ${TIMEOUT_SECS}s"
    fi

    # sanitizer 运行环境契约（P5 收口执行期发现，详见 .qoder/plans/Sanitizer统一收口计划P5.md）:
    # 1) suppressions 必须走 LSAN_OPTIONS（ASAN_OPTIONS 传参在本机 GCC libasan 上对
    #    任何非空 suppression 文件均 parse 失败并退出码 1）
    # 2) TSan 须禁用 ASLR（本机内核 high-entropy ASLR 下 TSan runtime 启动即
    #    "unexpected memory mapping" 退出码 66）
    # 每轮环境隔离: 只保留本轮需要的 sanitizer 变量（多轮顺序执行时防上一轮残留）
    SAN_RUNNER=()
    case "${SANITIZER}" in
        asan)
            unset TSAN_OPTIONS
            export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
            export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
            export LSAN_OPTIONS="suppressions=${REPO_DIR}/Test/YomkServer/lsan.supp"
            echo "-- sanitizer: asan（ASan+UBSan 插桩；suppressions 走 LSAN_OPTIONS）"
            ;;
        tsan)
            unset ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS
            export TSAN_OPTIONS="halt_on_error=1"
            SAN_RUNNER=(setarch "$(uname -m)" -R)
            echo "-- sanitizer: tsan（setarch -R 禁用 ASLR 包裹运行）"
            ;;
        off)
            unset ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS TSAN_OPTIONS
            ;;
    esac

    local log_root="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)_${SANITIZER}"
    mkdir -p "${log_root}"
    local summary="${log_root}/summary.log"
    printf "[MODE] sanitizer=%s bin=%s\n" "${SANITIZER}" "${BIN_DIR}" >> "${summary}"

    echo "-- 测试可执行目录: ${BIN_DIR}"
    echo "-- 日志目录:       ${log_root}"
    echo "-- 测试总数:       ${#TESTS[@]}"
    echo ""

    local total=${#TESTS[@]}
    local passed=0
    local idx=0
    local t log rc start elapsed
    for t in "${TESTS[@]}"; do
        idx=$((idx + 1))
        log="${log_root}/${t}.log"
        CURRENT_WORKDIR="$(mktemp -d)"
        printf "[%2d/%2d] %-32s " "${idx}" "${total}" "${t}"
        start=${SECONDS}
        ( cd "${CURRENT_WORKDIR}" && timeout "${TIMEOUT_SECS}" "${SAN_RUNNER[@]}" "${BIN_DIR}/${t}" ) > "${log}" 2>&1
        rc=$?
        elapsed=$((SECONDS - start))
        cleanup_workdir
        if [ ${rc} -eq 0 ]; then
            echo "PASS (${elapsed}s)"
            printf "[PASS] %-32s %ds\n" "${t}" "${elapsed}" >> "${summary}"
            passed=$((passed + 1))
        else
            local reason="退出码 ${rc}"
            [ ${rc} -eq 124 ] && reason="超时(>${TIMEOUT_SECS}s)"
            echo "FAIL (${reason}, ${elapsed}s)"
            printf "[FAIL] %-32s %s\n" "${t}" "${reason}" >> "${summary}"
            echo "-------------------------------------------"
            echo "测试 ${t} 失败: ${reason}"
            echo "完整日志: ${log}"
            local fail_count
            fail_count=$(grep -c "\[FAIL\]" "${log}" 2>/dev/null || true)
            if [ -n "${fail_count}" ] && [ "${fail_count}" -gt 0 ]; then
                echo "失败用例（共 ${fail_count} 行 [FAIL]，摘要如下）:"
                grep "\[FAIL\]" "${log}" | head -20
            else
                echo "（无 [FAIL] 行，可能为崩溃/超时，请查看完整日志）"
                tail -30 "${log}"
            fi
            echo "-------------------------------------------"
            exit 1
        fi
    done

    # 现场残留验收
    if ! check_residue | tee -a "${summary}"; then
        echo ""
        echo "==========================================="
        echo " 测试后现场残留检查未通过，整体判定失败"
        echo "==========================================="
        exit 1
    fi

    echo ""
    echo "==========================================="
    echo " YomkServer 全量测试通过: ${passed}/${total}"
    echo " 总耗时: ${SECONDS}s"
    echo " 日志目录: ${log_root}"
    echo "==========================================="
    echo "YomkServer 全量测试通过: ${passed}/${total}" >> "${summary}"
    return 0
}

main "$@"
