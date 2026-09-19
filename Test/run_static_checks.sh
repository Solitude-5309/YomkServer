#!/bin/bash
# YomkServer 静态代码检查运行器（与 Test/run_tests.sh 成对：动态测试 + 静态检测）
# 用法:
#   ./run_static_checks.sh              默认双跑 cppcheck + clang-tidy
#   ./run_static_checks.sh --cppcheck   仅跑 cppcheck
#   ./run_static_checks.sh --tidy       仅跑 clang-tidy
#   ./run_static_checks.sh -h|--help    显示本帮助
# 行为:
#   1. cppcheck 档: 全量扫描 YomkServer/src（含全部 Modules）+ YomkServer/include，
#      warning/style/performance/portability/information 零告警验收（--error-exitcode=1）；
#      先打印检测文件清单，扫描时过滤显示逐文件 Checking 进度行与告警行（完整 verbose 落盘日志）
#   2. clang-tidy 档: 检查集与 Test/YomkServer/CMakeLists.txt 的 _YOMK_TIDY_CHECKS 对齐，
#      逐文件扫描 YomkServer/src 全部编译单元（[i/N] 进度与逐文件 [FAIL] 标记），
#      --warnings-as-errors 严格门禁，header-filter 限本仓库头
#   3. 任一工具告警 → 输出 [FAIL] 摘要并以非零码退出；全部零告警 → 输出 [PASS]
#   4. 日志落盘: Test/test_logs/<时间戳>/cppcheck.log + clang-tidy.log + summary.log
# 依赖: cppcheck、clang-tidy（缺失时报错并给出安装提示）、仓库根 compile_commands.json
#       （cmake 配置阶段自动导出，缺失时报错并给构建提示）
# 自愈: clang-tidy 档自动探测本机真实存在的 libstdc++ 头并显式 -isystem 注入，
#       修正"clang 按最新 GCC 探测但对应 libstdc++-dev 未装"导致的 file not found

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

RUN_TIDY=0
RUN_CPPCHECK=0
while [ $# -gt 0 ]; do
    case "$1" in
        --cppcheck) RUN_CPPCHECK=1 ;;
        --tidy)     RUN_TIDY=1 ;;
        -h|--help)  usage ;;
        *) echo "错误: 未知参数 $1"; usage ;;
    esac
    shift
done
if [ ${RUN_TIDY} -eq 0 ] && [ ${RUN_CPPCHECK} -eq 0 ]; then
    RUN_TIDY=1
    RUN_CPPCHECK=1
fi

if [ ${RUN_CPPCHECK} -eq 1 ] && ! command -v cppcheck >/dev/null 2>&1; then
    echo "错误: 未找到 cppcheck，请先安装: sudo apt-get install cppcheck"
    exit 1
fi
if [ ${RUN_TIDY} -eq 1 ]; then
    if ! command -v clang-tidy >/dev/null 2>&1; then
        echo "错误: 未找到 clang-tidy，请先安装: sudo apt-get install clang-tidy"
        exit 1
    fi
    if [ ! -f "${REPO_DIR}/compile_commands.json" ]; then
        echo "错误: ${REPO_DIR}/compile_commands.json 不存在，请先完成一次 CMake 配置（配置阶段自动导出）:"
        echo "  cmake -S ${REPO_DIR} -B ${REPO_DIR}/build"
        exit 1
    fi
fi

FAILED=0

# 日志落盘（与 YomkRpc/test/run_static_checks.sh 同形态）：test_logs/<时间戳>/ 下
# cppcheck.log / clang-tidy.log / summary.log
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
echo "-- 日志目录: ${LOG_ROOT}"

# ---------- cppcheck：库源码全量零告警验收（清单 + Checking 进度行，完整 verbose 落盘） ----------
# -I include/YomkServer 为真实构建路径（ELC5/FPC5 教训：缺此路径时裸 #include 解析失败，
# 宏未定义导致分析降级），抑制项与 Test/YomkServer/CMakeLists.txt 的 cppcheck 目标保持一致
if [ ${RUN_CPPCHECK} -eq 1 ]; then
    echo "-- cppcheck 检测文件清单（YomkServer/src + YomkServer/include，排除 build/ 生成物）:"
    mapfile -t CPPCHECK_FILES < <(find "${REPO_DIR}/YomkServer/src" "${REPO_DIR}/YomkServer/include" \
        -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -path '*/build/*' | sort)
    for f in "${CPPCHECK_FILES[@]}"; do
        echo "--   ${f#"${REPO_DIR}/"}"
    done
    echo "-- cppcheck 开始扫描（共 ${#CPPCHECK_FILES[@]} 个文件，Checking 行即逐文件进度，完整 verbose 日志落盘）..."
    CPPCHECK_OUT="${LOG_ROOT}/cppcheck.log"
    cppcheck --enable=warning,style,performance,portability,information \
        --std=c++17 --language=c++ \
        --verbose -j"$(nproc)" \
        --inline-suppr --suppress=missingInclude --suppress=noExplicitConstructor --suppress=useStlAlgorithm \
        --suppress=unmatchedSuppression \
        --template=gcc \
        -I "${REPO_DIR}/YomkServer/include" -I "${REPO_DIR}/YomkServer/src" -I "${REPO_DIR}/YomkServer/include/YomkServer" \
        --error-exitcode=1 \
        "${REPO_DIR}/YomkServer/src" "${REPO_DIR}/YomkServer/include" 2>&1 \
        | tee "${CPPCHECK_OUT}" \
        | grep --line-buffered -E '^Checking [^:]*\.\.\.|warning:|performance:|portability:|style:|information:|error:'
    rc=${PIPESTATUS[0]}
    if [ ${rc} -ne 0 ]; then
        echo "[FAIL] cppcheck 检出告警（退出码 ${rc}）:"
        grep -E "warning:|performance:|portability:|style:|information:|error:" "${CPPCHECK_OUT}" | head -20
        printf "[FAIL] cppcheck 退出码 %s\n" "${rc}" >> "${SUMMARY}"
        FAILED=1
    else
        echo "[PASS] cppcheck 零告警"
        printf "[PASS] cppcheck 零告警\n" >> "${SUMMARY}"
    fi
fi

# ---------- clang-tidy：src 逐文件扫描 + 本仓库头，零告警验收（[i/N] 进度） ----------
# 检查集为 _YOMK_TIDY_CHECKS 原串（排除项是校准成果，如框架宏约定触发的
# bugprone-macro-parentheses），修改时须与 Test/YomkServer/CMakeLists.txt 保持对齐
if [ ${RUN_TIDY} -eq 1 ]; then
    mapfile -t TIDY_FILES < <(find "${REPO_DIR}/YomkServer/src" -name '*.cpp' | sort)
    TIDY_TOTAL=${#TIDY_FILES[@]}
    echo "-- clang-tidy 扫描 YomkServer/src 全部编译单元（共 ${TIDY_TOTAL} 个，检查集: _YOMK_TIDY_CHECKS）..."
    # clang 标准库头解析自愈（同 clangd --query-driver 族教训：新机器 clang 按"最新已装 GCC"
    # 探测 libstdc++ 头 /usr/include/c++/<V>，若该版本 GCC 裸装而无 libstdc++-<V>-dev
    # （如 gcc-12 + 仅 libstdc++-11-dev），标准库头全部 file not found，并在残缺 AST 上
    # 连带大量假告警（member-init/init-variables 等）。此处按 <target>/版本倒序探测首个
    # 真实存在的 libstdc++，用 -isystem 显式注入；健康机器上注入路径与 clang 正常探测
    # 结果一致，无行为变化。
    TIDY_EXTRA_ARGS=()
    _GCC_TARGET="$(gcc -dumpmachine 2>/dev/null)"
    if [ -n "${_GCC_TARGET}" ] && [ -d "/usr/lib/gcc/${_GCC_TARGET}" ]; then
        for _v in $(ls "/usr/lib/gcc/${_GCC_TARGET}" | sort -rV); do
            if [ -d "/usr/include/c++/${_v}" ]; then
                TIDY_EXTRA_ARGS=(--extra-arg-before=-isystem "--extra-arg-before=/usr/include/c++/${_v}" \
                    --extra-arg-before=-isystem "--extra-arg-before=/usr/include/${_GCC_TARGET}/c++/${_v}" \
                    --extra-arg-before=-isystem "--extra-arg-before=/usr/include/c++/${_v}/backward")
                echo "-- clang 标准库头注入: libstdc++-${_v}（显式 -isystem 修正 GCC 探测与 libstdc++ dev 包错位）"
                break
            fi
        done
    fi
    if [ ${#TIDY_EXTRA_ARGS[@]} -eq 0 ]; then
        echo "错误: 未找到可用的 libstdc++ C++ 头（/usr/include/c++/<版本>），请安装对应 dev 包:"
        echo "  sudo apt-get install libstdc++-<gcc版本>-dev"
        exit 1
    fi
    TIDY_OUT="${LOG_ROOT}/clang-tidy.log"
    TIDY_FAIL=0
    TIDY_IDX=0
    for f in "${TIDY_FILES[@]}"; do
        TIDY_IDX=$((TIDY_IDX + 1))
        echo "-- [${TIDY_IDX}/${TIDY_TOTAL}] ${f#"${REPO_DIR}/"}"
        if ! clang-tidy -p "${REPO_DIR}" "${TIDY_EXTRA_ARGS[@]}" \
            --checks="bugprone-*,-bugprone-macro-parentheses,-bugprone-easily-swappable-parameters,cppcoreguidelines-*,-cppcoreguidelines-owning-memory,-cppcoreguidelines-macro-usage,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-explicit-virtual-functions,-cppcoreguidelines-non-private-member-variables-in-classes,-cppcoreguidelines-avoid-non-const-global-variables,clang-analyzer-*,performance-*,-performance-unnecessary-value-param,portability-*" \
            --header-filter='.*/YomkServer/(include|src)/.*' \
            --warnings-as-errors='*' \
            "${f}" >> "${TIDY_OUT}" 2>&1; then
            echo "    [FAIL] ${f#"${REPO_DIR}/"} 检出告警或编译错误"
            TIDY_FAIL=1
        fi
    done
    if [ ${TIDY_FAIL} -ne 0 ]; then
        echo "[FAIL] clang-tidy 检出告警:"
        grep -E "warning:|error:" "${TIDY_OUT}" | head -20
        printf "[FAIL] clang-tidy 告警\n" >> "${SUMMARY}"
        FAILED=1
    else
        echo "[PASS] clang-tidy 零告警"
        printf "[PASS] clang-tidy 零告警\n" >> "${SUMMARY}"
    fi
fi

if [ ${FAILED} -ne 0 ]; then
    echo "==========================================="
    echo " 静态代码检查未通过（日志目录: ${LOG_ROOT}）"
    echo "==========================================="
    echo "YomkServer 静态代码检查未通过" >> "${SUMMARY}"
    exit 1
fi
echo "==========================================="
echo " 静态代码检查全部通过（日志目录: ${LOG_ROOT}）"
echo "==========================================="
echo "YomkServer 静态代码检查全部通过" >> "${SUMMARY}"
exit 0
