#!/bin/bash
# YomkServer Ubuntu 一键编译安装脚本
# 用法: ./build_ubuntu.sh
# 功能:
#   1. 检查编译依赖(cmake/g++/make)，缺失时可选自动安装
#   2. 交互输入安装路径(默认 /opt/yomk)，编译并安装
#   3. 安装后创建环境变量 YOMK_PREFIX_PATH 记录安装路径
#   4. 将安装路径暴露到系统:
#      - /etc/profile.d/yomk.sh: 登录 shell（ssh/console）生效
#      - /etc/bash.bashrc 追加引用: 桌面终端等非登录交互式 shell 生效
#      - /etc/ld.so.conf.d/yomk.conf + ldconfig: 任意终端运行时可找到 libYomkServer.so

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DEFAULT_INSTALL_PREFIX="/opt/yomk"
PROFILE_SCRIPT="/etc/profile.d/yomk.sh"
BASHRC_FILE="/etc/bash.bashrc"
LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"

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

# -------------sl 4. 持久化 YOMK_PREFIX_PATH 并暴露 bin 目录 ----------------
echo "-- 写入环境变量配置: ${PROFILE_SCRIPT}"
sudo tee "${PROFILE_SCRIPT}" >/dev/null <<EOF
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PATH="\$YOMK_PREFIX_PATH/bin:\$PATH"
EOF

# /etc/profile.d/ 只对登录 shell（ssh、tty）生效，
# 桌面终端模拟器打开的是非登录交互式 shell，只读 /etc/bash.bashrc，
# 因此需在此追加引用，保证新开任意终端都能直接生效（幂等，重复执行不会叠加）
YOMK_BASHRC_MARK="# >>> YomkServer environment >>>"
if ! grep -qF "${YOMK_BASHRC_MARK}" "${BASHRC_FILE}" 2>/dev/null; then
    echo "-- 追加环境变量引用到: ${BASHRC_FILE}"
    sudo tee -a "${BASHRC_FILE}" >/dev/null <<EOF

${YOMK_BASHRC_MARK}
[ -f "${PROFILE_SCRIPT}" ] && . "${PROFILE_SCRIPT}"
# <<< YomkServer environment <<<
EOF
fi

# 当前会话立即生效
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PATH="${YOMK_PREFIX_PATH}/bin:${PATH}"
# -------------el 4. 持久化 YOMK_PREFIX_PATH 并暴露 bin 目录 ----------------

# -------------sl 5. 系统级暴露动态库搜索路径 ----------------
echo "-- 写入动态库搜索路径: ${LDCONF_FILE}"
echo "${INSTALL_PREFIX}/lib" | sudo tee "${LDCONF_FILE}" >/dev/null
sudo ldconfig || fail "ldconfig 执行失败"
# -------------el 5. 系统级暴露动态库搜索路径 ----------------

# -------------sl 6. 安装校验与完成提示 ----------------
echo "-- 校验安装结果..."
[ -f "${INSTALL_PREFIX}/lib/libYomkServer.so" ] \
    || fail "未找到 ${INSTALL_PREFIX}/lib/libYomkServer.so"

TEST_TARGETS=(TestYomkService TestYomkFunctionPool TestYomkContext TestYomkEventLoop TestYomkLogger)
for t in "${TEST_TARGETS[@]}"; do
    [ -f "${INSTALL_PREFIX}/bin/${t}" ] \
        || fail "未找到测试程序 ${INSTALL_PREFIX}/bin/${t}"
done

echo ""
echo "==========================================="
echo " YomkServer 安装成功!"
echo "-------------------------------------------"
echo " 安装路径:       ${INSTALL_PREFIX}"
echo " YOMK_PREFIX_PATH: ${YOMK_PREFIX_PATH}"
echo " 动态库缓存:"
ldconfig -p | grep -i YomkServer || true
echo "==========================================="
echo ""
echo "新开任意终端即可直接使用（无需手动 source）:"
echo "  - echo \$YOMK_PREFIX_PATH 查看安装路径"
echo "  - 直接运行 TestYomkService 等命令验证安装"
echo "当前终端立即生效请执行: source ${PROFILE_SCRIPT}"
# -------------el 6. 安装校验与完成提示 ----------------
