#!/bin/bash
# 第三方扩展安装脚本（交互式）
# 用法: bash install.sh
# 交互询问 YomkServerPy 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 扩展源树布局为 src/yomk/msg/extension_service/（消息）+ src/yomk/extension_service/（运行时），按 PEP 420 命名空间
# 并入框架 site-packages/yomk/（与框架子包平级共存，合并安装，不动框架文件）
# 安装后任意终端可 import yomk.extension_service 与 yomk.msg.extension_service

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 路径规范化：展开 ~ 、相对路径补全
_normalize_path() {
    local p="$1"
    p="${p/#\~/$HOME}"
    if [[ -n "${p}" && "${p}" != /* ]]; then
        p="$(pwd)/${p}"
    fi
    echo "${p}"
}

if [ -z "${YOMK_PREFIX_PATH}" ]; then
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServerPy，请手动输入安装路径"
fi

# 交互询问 YomkServerPy 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
read -r -p "请输入 YomkServerPy 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="$(_normalize_path "${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}")"
unset _INPUT_PREFIX
if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServerPy 安装路径"
    exit 1
fi
echo "-- YomkServerPy 安装路径: ${YOMK_SERVER_PATH}"

# 探测 site-packages 目录（lib/pythonX.Y/site-packages）
SITE_PACKAGES="$(ls -d "${YOMK_SERVER_PATH}"/lib/python*/site-packages 2>/dev/null | head -n 1)"
if [ -z "${SITE_PACKAGES}" ]; then
    echo "错误: 未找到 site-packages 目录: ${YOMK_SERVER_PATH}/lib/pythonX.Y/site-packages"
    exit 1
fi

# 校验框架新包（PEP 420 命名空间包：yomk/ 顶层无 __init__.py，域入口就位）
if [ ! -f "${SITE_PACKAGES}/yomk/server/__init__.py" ] || [ ! -d "${SITE_PACKAGES}/yomk/msg" ]; then
    echo "错误: site-packages 中未找到 yomk 命名空间包: ${SITE_PACKAGES}"
    exit 1
fi
echo "-- 框架新包校验通过: ${SITE_PACKAGES}/yomk"

# 命名空间守卫：扩展仓库自身不得带 yomk/__init__.py 或 yomk/msg/__init__.py
# （会破坏 PEP 420 合并，遮蔽框架其它子包）
if [ -f "${SCRIPT_DIR}/src/yomk/__init__.py" ] || [ -f "${SCRIPT_DIR}/src/yomk/msg/__init__.py" ]; then
    echo "错误: 扩展源树含 yomk/__init__.py 或 yomk/msg/__init__.py，违反命名空间包纪律，禁止安装"
    exit 1
fi

# 安装目录不可写时（如 /opt/yomk）使用 sudo 执行安装
SUDO=""
if [ ! -w "${SITE_PACKAGES}" ]; then
    SUDO="sudo"
fi

# 合并安装（幂等覆盖：仅落扩展自身文件，不删框架文件）
if [ -d "${SITE_PACKAGES}/yomk/extension_service" ]; then
    echo "-- 移除旧版本: ${SITE_PACKAGES}/yomk/extension_service"
    ${SUDO} rm -rf "${SITE_PACKAGES}/yomk/extension_service"
fi
if [ -d "${SITE_PACKAGES}/yomk/msg/extension_service" ]; then
    echo "-- 移除旧版本: ${SITE_PACKAGES}/yomk/msg/extension_service"
    ${SUDO} rm -rf "${SITE_PACKAGES}/yomk/msg/extension_service"
fi
${SUDO} cp -R "${SCRIPT_DIR}/src/yomk/extension_service" "${SITE_PACKAGES}/yomk/"
${SUDO} cp -R "${SCRIPT_DIR}/src/yomk/msg/extension_service" "${SITE_PACKAGES}/yomk/msg/"

# 安装后验证：新终端等价环境（仅 PYTHONPATH 指向 site-packages）下导入扩展域
if PYTHONPATH="${SITE_PACKAGES}" python3 -c "import yomk.extension_service, yomk.msg.extension_service; print('扩展版本:', yomk.extension_service.__version__)" 2>/dev/null; then
    echo "-- 扩展导入验证通过"
else
    echo "错误: 扩展安装后无法导入，错误详情:"
    PYTHONPATH="${SITE_PACKAGES}" python3 -c "import yomk.extension_service, yomk.msg.extension_service"
    exit 1
fi

echo "安装完成，扩展已并入框架 yomk 命名空间，新开任意终端即可 import yomk.extension_service / yomk.msg.extension_service"
echo "可运行验证: python3 ${SCRIPT_DIR}/test/test_template_extension.py"
