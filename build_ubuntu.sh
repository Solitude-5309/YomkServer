#!/bin/bash
# YomkServerPy Ubuntu 一键编译安装脚本
# 用法: ./build_ubuntu.sh
# 功能:
#   1. 检查编译依赖(cmake/gcc/python3/cython + Python.h + readerwriterlock)，缺失时可选自动安装
#   2. 交互输入安装路径(默认 /opt/yomk)，固定 Release 编译并安装
#   3. 安装后调用 scripts/install_env.sh 完成系统级配置（与 README 手动 CMake 安装共用同一脚本，
#      保证两种安装方式最终结果完全一致）:
#      - 创建环境变量 YOMK_PREFIX_PATH 记录安装路径
#      - 创建 PYTHONPATH 指向安装产物 lib/pythonX.Y/site-packages
#   4. 将安装路径暴露到系统:
#      - /etc/profile.d/yomkpy.sh: 登录 shell（ssh/console）生效
#      - /etc/bash.bashrc 追加引用: 桌面终端等非登录交互式 shell 生效
#      新开任意终端即可直接 import YomkServerPy（无需手动 export PYTHONPATH）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DEFAULT_INSTALL_PREFIX="/opt/yomk"
PROFILE_SCRIPT="/etc/profile.d/yomkpy.sh"

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 依赖检查 ----------------
check_dependencies() {
    local missing_pkgs=()
    local missing_pips=()
    # 依赖命令 -> apt 包名
    declare -A cmd_pkg_map=(
        [cmake]="cmake"
        [gcc]="gcc"
        [python3]="python3"
    )

    for cmd in cmake gcc python3; do
        if ! command -v "${cmd}" >/dev/null 2>&1; then
            missing_pkgs+=("${cmd_pkg_map[${cmd}]}")
        fi
    done

    # cython 命令行工具（Cython 源码 -> C 转译），由 pip 包提供
    if ! command -v cython >/dev/null 2>&1; then
        missing_pips+=("cython")
    fi

    # Python 开发头文件（Python.h，编译扩展必需）
    if ! python3 -c "import sysconfig, os; assert os.path.exists(os.path.join(sysconfig.get_paths()['include'], 'Python.h'))" >/dev/null 2>&1; then
        missing_pkgs+=("python3-dev")
    fi

    # Python 运行时依赖（YomkServerPy import readerwriterlock）
    if ! python3 -c "import readerwriterlock" >/dev/null 2>&1; then
        missing_pips+=("readerwriterlock")
    fi

    if [ ${#missing_pkgs[@]} -eq 0 ] && [ ${#missing_pips[@]} -eq 0 ]; then
        echo "-- 依赖检查通过: cmake, gcc, python3, python3-dev, cython, readerwriterlock 均已安装"
        return 0
    fi

    echo "-- 检测到缺失以下依赖:"
    for pkg in "${missing_pkgs[@]:-}"; do
        [ -n "${pkg}" ] && echo "   - ${pkg} (apt)"
    done
    for pkg in "${missing_pips[@]:-}"; do
        [ -n "${pkg}" ] && echo "   - ${pkg} (pip)"
    done

    # 非交互环境无法询问，按「否」处理
    if [ ! -t 0 ]; then
        echo "错误: 非交互环境无法自动安装依赖，请手动执行:" >&2
        [ ${#missing_pkgs[@]} -gt 0 ] && echo "   sudo apt-get install -y ${missing_pkgs[*]}" >&2
        [ ${#missing_pips[@]} -gt 0 ] && echo "   pip install ${missing_pips[*]}" >&2
        exit 1
    fi

    read -r -p "是否自动安装缺失依赖？[Y/n]，默认: Y " answer
    case "${answer}" in
        [nN]*)
            echo "已取消自动安装，请手动执行以下命令后重新运行本脚本:"
            [ ${#missing_pkgs[@]} -gt 0 ] && echo "   sudo apt-get install -y ${missing_pkgs[*]}"
            [ ${#missing_pips[@]} -gt 0 ] && echo "   pip install ${missing_pips[*]}"
            exit 1
            ;;
        *)
            if [ ${#missing_pkgs[@]} -gt 0 ]; then
                echo "-- 开始自动安装依赖: ${missing_pkgs[*]}"
                sudo apt-get update || fail "apt-get update 失败"
                sudo apt-get install -y "${missing_pkgs[@]}" || fail "依赖安装失败"
            fi
            if [ ${#missing_pips[@]} -gt 0 ]; then
                echo "-- 开始自动安装依赖: pip install ${missing_pips[*]}"
                pip install "${missing_pips[@]}" || fail "依赖安装失败"
            fi
            # 安装后再次校验
            for cmd in cmake gcc python3 cython; do
                command -v "${cmd}" >/dev/null 2>&1 || fail "依赖 ${cmd} 安装后仍不可用"
            done
            python3 -c "import readerwriterlock" >/dev/null 2>&1 || fail "依赖 readerwriterlock 安装后仍不可用"
            echo "-- 依赖安装完成"
            ;;
    esac
}
# -------------el 1. 依赖检查 ----------------

check_dependencies

# -------------sl 1.5 编译环境检测 ----------------
detect_environment() {
    echo "-- 检测编译环境..."

    # 发行版检测（/etc/os-release，覆盖 Ubuntu/Debian/CentOS/openEuler 等）
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        echo "   - 发行版:   ${PRETTY_NAME:-${ID:-未知}} (ID=${ID:-unknown}${VERSION_ID:+, VERSION_ID=${VERSION_ID}})"
        case "${ID:-unknown}" in
            ubuntu|debian|linuxmint) ;;
            *) echo "   - 注意: 当前发行版非 Debian/Ubuntu 系，本脚本依赖自动安装走 apt/pip，其他发行版请手动安装依赖" ;;
        esac
    else
        echo "   - 发行版:   未知（未找到 /etc/os-release）"
    fi
    echo "   - 内核:     $(uname -r)"
    echo "   - 架构:     $(uname -m)"
    echo "   - Python:   $(python3 --version 2>&1)"
    echo "   - Cython:   $(cython --version 2>&1)"
    echo "   - 编译器:   $(gcc --version | head -1)"
    echo "   - CMake:    $(cmake --version | head -1)"
    echo "-- 完整环境指纹将在 cmake 配置阶段输出，并生成 abi_report.txt 随库安装"
}
detect_environment
# -------------el 1.5 编译环境检测 ----------------

# -------------sl 2. 交互确定安装路径 ----------------
read -r -p "请输入安装路径 [默认: ${DEFAULT_INSTALL_PREFIX}]: " input_prefix
INSTALL_PREFIX="${input_prefix:-${DEFAULT_INSTALL_PREFIX}}"

# 展开 ~ 为 $HOME
INSTALL_PREFIX="${INSTALL_PREFIX/#\~/$HOME}"
# 非绝对路径自动补全为基于当前目录的绝对路径
if [[ "${INSTALL_PREFIX}" != /* ]]; then
    INSTALL_PREFIX="$(pwd)/${INSTALL_PREFIX}"
fi
# 去除末尾的 /（根目录除外）
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
[ -z "${INSTALL_PREFIX}" ] && INSTALL_PREFIX="/"

echo "-- 安装路径: ${INSTALL_PREFIX}"
# -------------el 2. 交互确定安装路径 ----------------

# -------------sl 3. 配置、编译、安装 ----------------
echo "-- 开始配置..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    || fail "cmake 配置失败"

echo "-- 开始编译..."
cmake --build "${BUILD_DIR}" -j"$(nproc)" || fail "编译失败"

echo "-- 开始安装..."
sudo cmake --install "${BUILD_DIR}" || fail "安装失败"
# -------------el 3. 配置、编译、安装 ----------------

# -------------sl 4. 安装后系统级配置（共享脚本，与手动 CMake 安装完全一致） ----------------
# 环境变量持久化与安装校验逻辑抽至 scripts/install_env.sh，
# 由本脚本与 README 手动 CMake 安装共用，保证两种安装方式最终结果完全一致
echo "-- 执行安装后系统级配置（环境变量 / 安装校验）..."
"${SCRIPT_DIR}/scripts/install_env.sh" "${INSTALL_PREFIX}" || fail "安装后环境配置失败"
# 子进程内的 export 不会带回本脚本，此处同步当前会话变量
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
PY_SITE_DIR=$(ls -d "${INSTALL_PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
if [ -n "${PY_SITE_DIR}" ]; then
    export PYTHONPATH="${PY_SITE_DIR}:${PYTHONPATH}"
fi
# -------------el 4. 安装后系统级配置（与手动 CMake 安装共用 scripts/install_env.sh） ----------------

# -------------sl 5. 安装成功提示 ----------------
# 安装结果校验（YomkServerPy.so / YomkApi.py / YomkServerPy.pyi）已由 scripts/install_env.sh 完成
echo ""
echo "==========================================="
echo " YomkServerPy 安装成功!"
echo "-------------------------------------------"
echo " 安装路径:         ${INSTALL_PREFIX}"
echo " YOMK_PREFIX_PATH: ${YOMK_PREFIX_PATH}"
echo " PYTHONPATH:       ${PY_SITE_DIR}"
echo " 编译器:           $(gcc --version | head -1)"
echo " 环境指纹报告:     ${INSTALL_PREFIX}/share/YomkServerPy/abi_report.txt"
echo " Python 模块（安装于 ${PY_SITE_DIR}）:"
for f in YomkServerPy.so YomkApi.py YomkServerPy.pyi; do
    echo "   - ${f}"
done
echo " 示例（不随库安装，位于源码树 examples/ 目录，进入源码目录直接运行）:"
echo "   - examples/ExampleYomkService.py"
echo "   - examples/ExampleYomkContext.py"
echo "   - examples/ExampleYomkFunctionPool.py"
echo "   - examples/ExampleYomkEventLoop.py"
echo "==========================================="
echo ""
echo "新开任意终端即可直接使用（无需手动 source）:"
echo "  - echo \$YOMK_PREFIX_PATH 查看安装路径"
echo "  - python3 -c 'import YomkServerPy' 验证安装"
echo "  - ./test/run_tests.sh 运行全量测试"
echo "当前终端立即生效请执行: source ${PROFILE_SCRIPT}"
# -------------el 5. 安装成功提示 ----------------
