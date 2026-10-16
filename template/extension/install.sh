#!/bin/bash
# 扩展安装脚本（交互式）
# 用法: bash install.sh
# 交互询问 YomkServerPy 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 将扩展包安装到框架 site-packages（与框架安装到一起），安装后任意终端可 import TemplateExtension

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXTENSION_NAME="TemplateExtension"

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

# 校验框架模块（扩展 YomkServerPy*.so/.pyd + 门面 YomkApi.py）
shopt -s nullglob
_EXT_FILES=("${SITE_PACKAGES}"/YomkServerPy*.so "${SITE_PACKAGES}"/YomkServerPy*.pyd)
shopt -u nullglob
if [ ${#_EXT_FILES[@]} -eq 0 ] || [ ! -f "${SITE_PACKAGES}/YomkApi.py" ]; then
    echo "错误: site-packages 中未找到 YomkServerPy 扩展模块或 YomkApi.py: ${SITE_PACKAGES}"
    unset _EXT_FILES
    exit 1
fi
unset _EXT_FILES
echo "-- 框架模块校验通过: ${SITE_PACKAGES}"

# 安装目录不可写时（如 /opt/yomk）使用 sudo 执行安装
SUDO=""
if [ ! -w "${SITE_PACKAGES}" ]; then
    SUDO="sudo"
fi

# 安装扩展包（幂等覆盖旧版本）
if [ -d "${SITE_PACKAGES}/${EXTENSION_NAME}" ]; then
    echo "-- 移除旧版本: ${SITE_PACKAGES}/${EXTENSION_NAME}"
    ${SUDO} rm -rf "${SITE_PACKAGES}/${EXTENSION_NAME}"
fi
${SUDO} cp -r "${SCRIPT_DIR}/${EXTENSION_NAME}" "${SITE_PACKAGES}/"

# 安装后验证：新终端等价环境（仅 PYTHONPATH 指向 site-packages）下导入扩展
if PYTHONPATH="${SITE_PACKAGES}" python3 -c "import ${EXTENSION_NAME}; print('扩展版本:', ${EXTENSION_NAME}.EXTENSION_VERSION)" 2>/dev/null; then
    echo "-- 扩展导入验证通过"
else
    echo "错误: 扩展安装后无法导入，错误详情:"
    PYTHONPATH="${SITE_PACKAGES}" python3 -c "import ${EXTENSION_NAME}"
    exit 1
fi

echo "安装完成，扩展已装入框架 site-packages，新开任意终端即可 import ${EXTENSION_NAME}"
echo "可运行验证: python3 ${SCRIPT_DIR}/test/test_template_extension.py"
