#!/bin/bash
# YomkServer 安装后系统级配置（共享脚本，单一事实来源）
# 用法: ./scripts/install_env.sh [安装路径]（缺省 /opt/yomk）
# 说明: build_ubuntu.sh 自动安装与 README 手动 CMake 安装共用本脚本，
#       保证两种安装方式的最终结果完全一致:
#   1. 校验安装结果: libYomkServer.so 与全部示例程序存在
#   2. 持久化环境变量 YOMK_PREFIX_PATH 并暴露 bin 目录:
#      - /etc/profile.d/yomk.sh: 登录 shell（ssh/console）生效
#      - /etc/bash.bashrc 追加引用: 桌面终端等非登录交互式 shell 生效
#   3. 系统级注册动态库搜索路径: /etc/ld.so.conf.d/yomk.conf + ldconfig，
#      任意终端运行时可找到 libYomkServer.so
# 环境开关: YOMK_ENV_SKIP_SYSTEM=1 跳过全部 /etc 写入与 ldconfig
#          （用于安装树对比验证或无 root 权限场景，仅执行安装校验）

set -e

DEFAULT_INSTALL_PREFIX="/opt/yomk"
PROFILE_SCRIPT="/etc/profile.d/yomk.sh"
BASHRC_FILE="/etc/bash.bashrc"
LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 0. 解析安装路径 ----------------
INSTALL_PREFIX="${1:-${DEFAULT_INSTALL_PREFIX}}"

# 展开 ~ 为 $HOME
INSTALL_PREFIX="${INSTALL_PREFIX/#\~/$HOME}"
# 非绝对路径自动补全为基于当前目录的绝对路径
if [[ "${INSTALL_PREFIX}" != /* ]]; then
    INSTALL_PREFIX="$(pwd)/${INSTALL_PREFIX}"
fi
# 去除末尾的 /（根目录除外）
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
[ -z "${INSTALL_PREFIX}" ] && INSTALL_PREFIX="/"
# -------------el 0. 解析安装路径 ----------------

# -------------sl 1. 校验安装结果 ----------------
[ -f "${INSTALL_PREFIX}/lib/libYomkServer.so" ] \
    || fail "未找到 ${INSTALL_PREFIX}/lib/libYomkServer.so，请先完成 cmake install"

EXAMPLE_TARGETS=(ExampleYomkService ExampleYomkFunctionPool ExampleYomkContext ExampleYomkEventLoop ExampleYomkLogger)
for t in "${EXAMPLE_TARGETS[@]}"; do
    [ -f "${INSTALL_PREFIX}/bin/${t}" ] \
        || fail "未找到示例程序 ${INSTALL_PREFIX}/bin/${t}，请先完成 cmake install"
done
echo "-- 安装结果校验通过: libYomkServer.so + ${#EXAMPLE_TARGETS[@]} 个示例程序"
# -------------el 1. 校验安装结果 ----------------

# -------------sl 2. 系统级配置（环境变量 + 动态库路径） ----------------
if [ "${YOMK_ENV_SKIP_SYSTEM}" = "1" ]; then
    echo "-- YOMK_ENV_SKIP_SYSTEM=1: 跳过 /etc 写入与 ldconfig（仅执行安装校验）"
else
    # /etc/profile.d/ 只对登录 shell（ssh、tty）生效，
    # 桌面终端模拟器打开的是非登录交互式 shell，只读 /etc/bash.bashrc，
    # 因此需同时配置两处，保证新开任意终端都能直接生效（幂等，重复执行不会叠加）
    echo "-- 写入环境变量配置: ${PROFILE_SCRIPT}"
    sudo tee "${PROFILE_SCRIPT}" >/dev/null <<EOF
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PATH="\$YOMK_PREFIX_PATH/bin:\$PATH"
EOF

    YOMK_BASHRC_MARK="# >>> YomkServer environment >>>"
    if ! grep -qF "${YOMK_BASHRC_MARK}" "${BASHRC_FILE}" 2>/dev/null; then
        echo "-- 追加环境变量引用到: ${BASHRC_FILE}"
        sudo tee -a "${BASHRC_FILE}" >/dev/null <<EOF

${YOMK_BASHRC_MARK}
[ -f "${PROFILE_SCRIPT}" ] && . "${PROFILE_SCRIPT}"
# <<< YomkServer environment <<<
EOF
    fi

    # 系统级暴露动态库搜索路径，任意终端任意进程均可解析 libYomkServer.so
    echo "-- 写入动态库搜索路径: ${LDCONF_FILE}"
    echo "${INSTALL_PREFIX}/lib" | sudo tee "${LDCONF_FILE}" >/dev/null
    sudo ldconfig || fail "ldconfig 执行失败"
fi
# -------------el 2. 系统级配置 ----------------

# -------------sl 3. 当前会话立即生效与完成摘要 ----------------
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PATH="${YOMK_PREFIX_PATH}/bin:${PATH}"

echo "-- 安装后配置完成:"
echo "   - 环境变量:   YOMK_PREFIX_PATH=${YOMK_PREFIX_PATH}"
echo "   - 动态库:     ${INSTALL_PREFIX}/lib/libYomkServer.so"
echo "   - 示例程序:   ${INSTALL_PREFIX}/bin/{${EXAMPLE_TARGETS[*]}}"
if [ "${YOMK_ENV_SKIP_SYSTEM}" != "1" ]; then
    echo "   - 生效范围:   新开任意终端免 source 直接可用"
    echo "   - 当前终端立即生效请执行: source ${PROFILE_SCRIPT}"
fi
# -------------el 3. 当前会话立即生效与完成摘要 ----------------
