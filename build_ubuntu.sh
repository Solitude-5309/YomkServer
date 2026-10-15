#!/bin/bash
# YomkServer Ubuntu 一键编译安装脚本
# 用法: ./build_ubuntu.sh
# 功能:
#   1. 检查编译依赖(cmake/g++/make)，缺失时可选自动安装
#   2. 交互输入安装路径(默认 /opt/yomk)，交互询问是否编译内部测试（默认不编译，输入 Y 才编译；测试仅编译，不安装），编译并安装（库 + Examples 示例）
#   3. 安装后调用 scripts/install_env.sh 完成系统级配置（与 README 手动 CMake 安装共用同一脚本，
#      保证两种安装方式最终结果完全一致）:
#      - 创建环境变量 YOMK_PREFIX_PATH 记录安装路径
#   4. 将安装路径暴露到系统:
#      - /etc/profile.d/yomk.sh: 登录 shell（ssh/console）生效
#      - /etc/bash.bashrc 追加引用: 桌面终端等非登录交互式 shell 生效
#      - /etc/ld.so.conf.d/yomk.conf + ldconfig: 任意终端运行时可找到 libYomkServer.so

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DEFAULT_INSTALL_PREFIX="/opt/yomk"
PROFILE_SCRIPT="/etc/profile.d/yomk.sh"

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 依赖检查 ----------------
check_dependencies() {
    local missing_pkgs=()
    # 依赖命令 -> apt 包名
    declare -A cmd_pkg_map=(
        [cmake]="cmake"
        [g++]="g++"
        [make]="make"
    )

    for cmd in cmake g++ make; do
        if ! command -v "${cmd}" >/dev/null 2>&1; then
            missing_pkgs+=("${cmd_pkg_map[${cmd}]}")
        fi
    done

    if [ ${#missing_pkgs[@]} -eq 0 ]; then
        echo "-- 依赖检查通过: cmake, g++, make 均已安装"
        return 0
    fi

    echo "-- 检测到缺失以下依赖:"
    for pkg in "${missing_pkgs[@]}"; do
        echo "   - ${pkg}"
    done

    # 非交互环境无法询问，按「否」处理
    if [ ! -t 0 ]; then
        echo "错误: 非交互环境无法自动安装依赖，请手动执行:" >&2
        echo "   sudo apt-get install -y ${missing_pkgs[*]}" >&2
        exit 1
    fi

    read -r -p "是否自动安装缺失依赖？[Y/n]，默认: Y " answer
    case "${answer}" in
        [nN]*)
            echo "已取消自动安装，请手动执行以下命令后重新运行本脚本:"
            echo "   sudo apt-get install -y ${missing_pkgs[*]}"
            exit 1
            ;;
        *)
            echo "-- 开始自动安装依赖: ${missing_pkgs[*]}"
            sudo apt-get update || fail "apt-get update 失败"
            sudo apt-get install -y "${missing_pkgs[@]}" || fail "依赖安装失败"
            # 安装后再次校验
            for cmd in cmake g++ make; do
                command -v "${cmd}" >/dev/null 2>&1 || fail "依赖 ${cmd_pkg_map[${cmd}]} 安装后仍不可用"
            done
            echo "-- 依赖安装完成"
            ;;
    esac
}
# -------------el 1. 依赖检查 ----------------

check_dependencies

# -------------sl 1.5 编译环境检测（ABI 指纹） ----------------
detect_environment() {
    echo "-- 检测编译环境..."

    # 发行版检测（/etc/os-release，覆盖 Ubuntu/Debian/CentOS/openEuler 等）
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        echo "   - 发行版:   ${PRETTY_NAME:-${ID:-未知}} (ID=${ID:-unknown}${VERSION_ID:+, VERSION_ID=${VERSION_ID}})"
        case "${ID:-unknown}" in
            ubuntu|debian|linuxmint) ;;
            *) echo "   - 注意: 当前发行版非 Debian/Ubuntu 系，本脚本依赖自动安装走 apt，其他发行版请手动安装依赖" ;;
        esac
    else
        echo "   - 发行版:   未知（未找到 /etc/os-release）"
    fi
    echo "   - 内核:     $(uname -r)"
    echo "   - 架构:     $(uname -m)"

    # 编译器选择：g++ 优先，clang++ 回退（与 CMake/AbiInfo.cmake 逻辑一致）
    if command -v g++ >/dev/null 2>&1; then
        CXX_COMPILER_BIN="$(command -v g++)"
    elif command -v clang++ >/dev/null 2>&1; then
        CXX_COMPILER_BIN="$(command -v clang++)"
        echo "   - 注意: 未找到 g++，回退使用 clang++（ABI 与 g++ 产物不通用，请下游保持一致）"
    else
        fail "未找到 g++ 或 clang++，请先安装: sudo apt-get install g++"
    fi
    echo "   - 编译器:   ${CXX_COMPILER_BIN} ($("${CXX_COMPILER_BIN}" --version | head -1))"
    echo "   - CMake:    $(cmake --version | head -1)"
    echo "   - Make:     $(make --version | head -1)"
    echo "-- 完整环境指纹将在 cmake 配置阶段输出，并生成 abi_report.txt 随库安装"
}
detect_environment
# -------------el 1.5 编译环境检测（ABI 指纹） ----------------

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

# -------------sl 2.5 交互确定是否编译内部测试 ----------------
# 非交互环境无法询问，默认不编译测试
if [ ! -t 0 ]; then
    BUILD_TESTS=OFF
    echo "-- 非交互环境，默认不编译内部测试"
else
    read -r -p "是否编译框架内部测试？输入 Y 编译，直接回车默认不编译 [y/N]: " answer
    case "${answer}" in
        [yY]*) BUILD_TESTS=ON ;;
        *) BUILD_TESTS=OFF ;;
    esac
fi
echo "-- 编译内部测试: ${BUILD_TESTS}"
# -------------el 2.5 交互确定是否编译内部测试 ----------------

# -------------sl 3. 配置、编译、安装 ----------------
echo "-- 开始配置..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER_BIN}" \
    -DYOMK_BUILD_TESTS="${BUILD_TESTS}" \
    || fail "cmake 配置失败"

echo "-- 开始编译..."
cmake --build "${BUILD_DIR}" -j"$(nproc)" || fail "编译失败"

echo "-- 开始安装..."
sudo cmake --install "${BUILD_DIR}" || fail "安装失败"
# -------------el 3. 配置、编译、安装 ----------------

# -------------sl 4. 安装后系统级配置（共享脚本，与手动 CMake 安装完全一致） ----------------
# 环境变量持久化、动态库搜索路径注册与安装校验逻辑抽至 scripts/install_env.sh，
# 由本脚本与 README 手动 CMake 安装共用，保证两种安装方式最终结果完全一致
echo "-- 执行安装后系统级配置（环境变量 / 动态库路径 / 安装校验）..."
"${SCRIPT_DIR}/scripts/install_env.sh" "${INSTALL_PREFIX}" || fail "安装后环境配置失败"
# 子进程内的 export 不会带回本脚本，此处同步当前会话变量
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PATH="${YOMK_PREFIX_PATH}/bin:${PATH}"
# -------------el 4. 安装后系统级配置（与手动 CMake 安装共用 scripts/install_env.sh） ----------------

# -------------sl 5. 安装成功提示 ----------------
# 安装结果校验（libYomkServer.so + 示例程序）已由 scripts/install_env.sh 完成
EXAMPLE_TARGETS=(ExampleYomkService ExampleYomkFunctionPool ExampleYomkContext ExampleYomkEventLoop ExampleYomkLogger)
echo ""
echo "==========================================="
echo " YomkServer 安装成功!"
echo "-------------------------------------------"
echo " 安装路径:       ${INSTALL_PREFIX}"
echo " YOMK_PREFIX_PATH: ${YOMK_PREFIX_PATH}"
echo " 编译器:         ${CXX_COMPILER_BIN} ($("${CXX_COMPILER_BIN}" --version | head -1))"
echo " 环境指纹报告:   ${INSTALL_PREFIX}/share/YomkServer/abi_report.txt"
echo " 动态库缓存:"
ldconfig -p | grep -i YomkServer || true
echo " 示例程序列表（安装于 ${INSTALL_PREFIX}/bin）:"
for t in "${EXAMPLE_TARGETS[@]}"; do
    echo "   - ${t}"
done
echo "==========================================="
echo ""
echo "新开任意终端即可直接使用（无需手动 source）:"
echo "  - echo \$YOMK_PREFIX_PATH 查看安装路径"
echo "  - 直接运行 ExampleYomkService 等命令验证安装"
if [ "${BUILD_TESTS}" = "ON" ]; then
    echo "内部测试程序已编译于 ${SCRIPT_DIR}/bin（仅供开发验证，不随安装部署）"
fi
echo "当前终端立即生效请执行: source ${PROFILE_SCRIPT}"
# -------------el 5. 安装成功提示 ----------------
