#!/bin/bash
# YomkServerPy 安装后系统级配置（共享脚本，单一事实来源）
# 用法: ./scripts/install_env.sh [安装路径]（缺省 /opt/yomk）
# 说明: build_ubuntu.sh 自动安装与 README 手动 CMake 安装共用本脚本，
#       保证两种安装方式的最终结果完全一致:
#   1. 校验安装结果: YomkServerPy.so / YomkApi.py / YomkServerPy.pyi 存在
#   2. 持久化环境变量 YOMK_PREFIX_PATH 与 PYTHONPATH（指向 lib/pythonX.Y/site-packages）:
#      - /etc/profile.d/yomkpy.sh: 登录 shell（ssh/console）生效
#      - /etc/bash.bashrc 追加引用: 桌面终端等非登录交互式 shell 生效
#      新开任意终端即可直接 import YomkServerPy
# 环境开关: YOMK_ENV_SKIP_SYSTEM=1 跳过全部 /etc 写入
#          （用于安装树对比验证或无 root 权限场景，仅执行安装校验）
# 说明: Python 扩展按完整路径 import，无需 ldconfig 注册系统库路径

set -e

DEFAULT_INSTALL_PREFIX="/opt/yomk"
PROFILE_SCRIPT="/etc/profile.d/yomkpy.sh"
BASHRC_FILE="/etc/bash.bashrc"

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
# 自动探测 lib/pythonX.Y/site-packages（X.Y 由构建时解释器版本决定）
PY_SITE_DIR=$(ls -d "${INSTALL_PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
[ -n "${PY_SITE_DIR}" ] \
    || fail "未找到 ${INSTALL_PREFIX}/lib/pythonX.Y/site-packages，请先完成 cmake install"

for f in YomkServerPy.so YomkServerPy.pyi YomkApi.py; do
    [ -f "${PY_SITE_DIR}/${f}" ] \
        || fail "未找到 ${PY_SITE_DIR}/${f}，请先完成 cmake install"
done

# 相对安装前缀的 site-packages 路径（写入 profile 脚本用）
PY_REL_SITE="${PY_SITE_DIR#"${INSTALL_PREFIX}/"}"
echo "-- 安装结果校验通过: ${PY_SITE_DIR}"
# -------------el 1. 校验安装结果 ----------------

# -------------sl 2. 系统级配置（环境变量 + PYTHONPATH） ----------------
if [ "${YOMK_ENV_SKIP_SYSTEM}" = "1" ]; then
    echo "-- YOMK_ENV_SKIP_SYSTEM=1: 跳过 /etc 写入（仅执行安装校验）"
else
    # /etc/profile.d/ 只对登录 shell（ssh、tty）生效，
    # 桌面终端模拟器打开的是非登录交互式 shell，只读 /etc/bash.bashrc，
    # 因此需同时配置两处，保证新开任意终端都能直接生效（幂等，重复执行不会叠加）
    echo "-- 写入环境变量配置: ${PROFILE_SCRIPT}"
    sudo tee "${PROFILE_SCRIPT}" >/dev/null <<EOF
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PYTHONPATH="\$YOMK_PREFIX_PATH/${PY_REL_SITE}:\$PYTHONPATH"
EOF

    YOMK_BASHRC_MARK="# >>> YomkServerPy environment >>>"
    if ! grep -qF "${YOMK_BASHRC_MARK}" "${BASHRC_FILE}" 2>/dev/null; then
        echo "-- 追加环境变量引用到: ${BASHRC_FILE}"
        sudo tee -a "${BASHRC_FILE}" >/dev/null <<EOF

${YOMK_BASHRC_MARK}
[ -f "${PROFILE_SCRIPT}" ] && . "${PROFILE_SCRIPT}"
# <<< YomkServerPy environment <<<
EOF
    fi
fi
# -------------el 2. 系统级配置 ----------------

# -------------sl 3. 当前会话立即生效与完成摘要 ----------------
export YOMK_PREFIX_PATH="${INSTALL_PREFIX}"
export PYTHONPATH="${PY_SITE_DIR}:${PYTHONPATH}"

echo "-- 安装后配置完成:"
echo "   - 环境变量:   YOMK_PREFIX_PATH=${YOMK_PREFIX_PATH}"
echo "   - PYTHONPATH: ${PY_SITE_DIR}"
echo "   - 模块文件:   ${PY_SITE_DIR}/{YomkServerPy.so, YomkApi.py, YomkServerPy.pyi}"
if [ "${YOMK_ENV_SKIP_SYSTEM}" != "1" ]; then
    echo "   - 生效范围:   新开任意终端免 source 直接 import YomkServerPy"
    echo "   - 当前终端立即生效请执行: source ${PROFILE_SCRIPT}"
fi
# -------------el 3. 当前会话立即生效与完成摘要 ----------------
