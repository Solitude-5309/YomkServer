#!/bin/bash
# YomkServerPy 静态代码检查运行器（与 test/run_tests.sh 成对：动态测试 + 静态检测）
# 用法:
#   ./run_static_checks.sh            默认双跑 ruff + mypy
#   ./run_static_checks.sh --ruff     仅跑 ruff
#   ./run_static_checks.sh --mypy     仅跑 mypy
#   ./run_static_checks.sh -h|--help  显示本帮助
# 行为:
#   1. ruff 档: 全量扫描 src/ 库源码，--isolated 消除用户级配置干扰，
#      规则集 E4/E7/E9/F（语法错误/可疑代码/F 未用导入等）零告警验收；
#      白名单: --ignore F401 —— YomkApi 伞门面 re-export 与 typing 冗余导入
#      属有意形态（11 处），登记为书面债务
#   2. mypy 档: 检查 src/YomkApi.py 与 src/YomkServerPy.py 两源文件，
#      --ignore-missing-imports + --follow-imports=silent（依赖面注解不作门禁）；
#      白名单: 豁免 4 个错误码 assignment(13)/misc(8)/attr-defined(2)/var-annotated(2)
#      合计 25 处 = src 未回填类型注解的书面债务，随注解回填逐步收窄
#   3. 任一工具检出告警 → 输出 [FAIL] 摘要并以非零码退出；全部零告警 → 输出 [PASS]
#   4. 日志落盘: test/test_logs/<时间戳>/ruff.log + mypy.log + summary.log
# 依赖: ruff、mypy（缺失时报错并给安装提示: python3 -m pip install --user ruff mypy）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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

# 日志落盘：test_logs/<时间戳>/ 下
# ruff.log / mypy.log / summary.log（与 run_tests.sh 的 test_logs/tmp 同居一层）
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
echo "-- 日志目录: ${LOG_ROOT}"

# ---------- ruff：src 库源码全量零告警验收（清单 + 逐文件进度由 ruff 自身输出承载） ----------
if [ ${RUN_RUFF} -eq 1 ]; then
    echo "-- ruff 检测文件清单（src/ 库源码全量）:"
    mapfile -t RUFF_FILES < <(find "${REPO_DIR}/src" -type f -name '*.py' | sort)
    for f in "${RUFF_FILES[@]}"; do
        echo "--   ${f#"${REPO_DIR}/"}"
    done
    echo "-- ruff 开始扫描（共 ${#RUFF_FILES[@]} 个文件，完整输出落盘）..."
    RUFF_OUT="${LOG_ROOT}/ruff.log"
    python3 -m ruff check --isolated --select E4,E7,E9,F --ignore F401 \
        "${REPO_DIR}/src" 2>&1 \
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

# ---------- mypy：两源文件类型检查零告警验收（白名单见文件头行为注记） ----------
if [ ${RUN_MYPY} -eq 1 ]; then
    echo "-- mypy 开始扫描（src/YomkApi.py + src/YomkServerPy.py）..."
    MYPY_OUT="${LOG_ROOT}/mypy.log"
    python3 -m mypy --ignore-missing-imports --follow-imports=silent \
        --cache-dir "${LOG_ROOT}/mypy_cache" \
        --disable-error-code=assignment \
        --disable-error-code=misc \
        --disable-error-code=attr-defined \
        --disable-error-code=var-annotated \
        "${REPO_DIR}/src/YomkApi.py" "${REPO_DIR}/src/YomkServerPy.py" 2>&1 \
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
