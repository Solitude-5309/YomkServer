#!/bin/bash
# YomkServer 静态代码检查运行器（与 Test/run_tests.sh 成对：动态测试 + 静态检测）
# 用法:
#   ./run_static_checks.sh              默认双跑 cppcheck + clang-tidy
#   ./run_static_checks.sh --cppcheck   仅跑 cppcheck
#   ./run_static_checks.sh --tidy       仅跑 clang-tidy
#   ./run_static_checks.sh -h|--help    显示本帮助
# 行为:
#   1. cppcheck 档: 全量扫描 YomkServer/src（含全部 Modules）+ YomkServer/include，
#      warning/style/performance/portability/information 零告警验收（--error-exitcode=1）
#   2. clang-tidy 档: 检查集与 Test/YomkServer/CMakeLists.txt 的 _YOMK_TIDY_CHECKS 对齐，
#      扫描 YomkServer/src 全部编译单元，--warnings-as-errors 严格门禁，header-filter 限本仓库头
#   3. 任一工具告警 → 输出 [FAIL] 摘要并以非零码退出；全部零告警 → 输出 [PASS]
# 依赖: cppcheck、clang-tidy（缺失时报错并给出安装提示）、仓库根 compile_commands.json
#       （cmake 配置阶段自动导出，缺失时报错并给构建提示）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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

# ---------- cppcheck：库源码全量零告警验收 ----------
# -I include/YomkServer 为真实构建路径（ELC5/FPC5 教训：缺此路径时裸 #include 解析失败，
# 宏未定义导致分析降级），抑制项与 Test/YomkServer/CMakeLists.txt 的 cppcheck 目标保持一致
if [ ${RUN_CPPCHECK} -eq 1 ]; then
    echo "-- cppcheck 全量扫描 YomkServer/src + YomkServer/include ..."
    CPPCHECK_OUT="$(mktemp)"
    cppcheck --enable=warning,style,performance,portability,information \
        --std=c++17 --language=c++ \
        --inline-suppr --suppress=missingInclude --suppress=noExplicitConstructor --suppress=useStlAlgorithm \
        --template=gcc \
        -I "${REPO_DIR}/YomkServer/include" -I "${REPO_DIR}/YomkServer/src" -I "${REPO_DIR}/YomkServer/include/YomkServer" \
        --error-exitcode=1 \
        "${REPO_DIR}/YomkServer/src" "${REPO_DIR}/YomkServer/include" > "${CPPCHECK_OUT}" 2>&1
    rc=$?
    if [ ${rc} -ne 0 ]; then
        echo "[FAIL] cppcheck 检出告警（退出码 ${rc}）:"
        grep -E "warning:|performance:|portability:|style:|information:|error:" "${CPPCHECK_OUT}" | head -20
        FAILED=1
    else
        echo "[PASS] cppcheck 零告警"
    fi
    rm -f "${CPPCHECK_OUT}"
fi

# ---------- clang-tidy：src 全部编译单元 + 本仓库头，零告警验收 ----------
# 检查集为 _YOMK_TIDY_CHECKS 原串（排除项是校准成果，如框架宏约定触发的
# bugprone-macro-parentheses），修改时须与 Test/YomkServer/CMakeLists.txt 保持对齐
if [ ${RUN_TIDY} -eq 1 ]; then
    echo "-- clang-tidy 扫描 YomkServer/src 全部编译单元 ..."
    mapfile -t TIDY_SRCS < <(find "${REPO_DIR}/YomkServer/src" -name '*.cpp' | sort)
    TIDY_OUT="$(mktemp)"
    clang-tidy -p "${REPO_DIR}" \
        --checks="bugprone-*,-bugprone-macro-parentheses,-bugprone-easily-swappable-parameters,cppcoreguidelines-*,-cppcoreguidelines-owning-memory,-cppcoreguidelines-macro-usage,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-explicit-virtual-functions,-cppcoreguidelines-non-private-member-variables-in-classes,-cppcoreguidelines-avoid-non-const-global-variables,clang-analyzer-*,performance-*,-performance-unnecessary-value-param,portability-*" \
        --header-filter='.*/YomkServer/(include|src)/.*' \
        --warnings-as-errors='*' \
        "${TIDY_SRCS[@]}" > "${TIDY_OUT}" 2>&1
    rc=$?
    if [ ${rc} -ne 0 ] || grep -qE "warning:" "${TIDY_OUT}"; then
        echo "[FAIL] clang-tidy 检出告警（退出码 ${rc}）:"
        grep -E "warning:|error:" "${TIDY_OUT}" | head -20
        FAILED=1
    else
        echo "[PASS] clang-tidy 零告警"
    fi
    rm -f "${TIDY_OUT}"
fi

if [ ${FAILED} -ne 0 ]; then
    echo "==========================================="
    echo " 静态代码检查未通过"
    echo "==========================================="
    exit 1
fi
echo "==========================================="
echo " 静态代码检查全部通过"
echo "==========================================="
exit 0
