#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh
# 依次交互询问 YomkServer 安装路径（前置路径）与扩展安装路径，默认均取环境变量 YOMK_PREFIX_PATH，可修改
# 扩展库与 YomkServer 安装到一起（头文件由 YomkServer::YomkServer 的 INTERFACE include 统一提供）
# 安装后将扩展 lib 注册到系统动态库搜索路径（复用 yomk.conf）并刷新 ldconfig 缓存，新开任意终端即可找到扩展 so

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="YomkExtension"
BUILD_DIR="${SCRIPT_DIR}/build"
TEST_DIR="${SCRIPT_DIR}/test"
TEST_BUILD_DIR="${TEST_DIR}/build"
_ORIG_DIR="$(pwd)"

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
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServer，请手动输入安装路径"
fi

# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
read -r -p "请输入 YomkServer 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="$(_normalize_path "${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}")"
if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServer 安装路径"
    return 1
fi
echo "-- YomkServer 安装路径: ${YOMK_SERVER_PATH}"

# 交互询问扩展安装路径，默认装入 YomkServer 安装目录（与 YomkServer 安装到一起）
read -r -p "请输入扩展安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_INSTALL
INSTALL_DIR="$(_normalize_path "${_INPUT_INSTALL:-${YOMK_PREFIX_PATH}}")"
unset _INPUT_PREFIX _INPUT_INSTALL
if [ -z "${INSTALL_DIR}" ]; then
    echo "错误: 未指定扩展安装路径"
    return 1
fi
echo "-- 扩展安装路径: ${INSTALL_DIR}"

# 安装目录不可写时（如 /opt/yomk）使用 sudo 执行安装
SUDO=""
if [ ! -w "${INSTALL_DIR}" ]; then
    SUDO="sudo"
fi

# 询问是否编译 test
read -p "编译测试程序? [Y/n]: " BUILD_TEST
BUILD_TEST=${BUILD_TEST:-y}
if [[ "${BUILD_TEST}" =~ ^[Yy]$ ]]; then
    BUILD_TEST="ON"
else
    BUILD_TEST="OFF"
fi

# 编译安装主库
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

cmake "${SCRIPT_DIR}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" -DCMAKE_PREFIX_PATH="${YOMK_SERVER_PATH}"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

${SUDO} cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 注册扩展库路径到系统动态库搜索路径（扩展属于 yomk，复用 yomk.conf，幂等追加）
YOMK_LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"
if ! grep -qxF "${INSTALL_DIR}/lib" "${YOMK_LDCONF_FILE}" 2>/dev/null; then
    echo "-- 注册动态库搜索路径: ${YOMK_LDCONF_FILE}"
    echo "${INSTALL_DIR}/lib" | sudo tee -a "${YOMK_LDCONF_FILE}" >/dev/null
fi
# 刷新动态库缓存：新增的 so 不会自动进入 ld.so.cache，必须重新执行 ldconfig
echo "-- 刷新动态库缓存 (ldconfig)..."
sudo ldconfig
if [ $? -ne 0 ]; then
    echo "ldconfig 执行失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 编译测试程序
if [ "${BUILD_TEST}" = "ON" ]; then
    mkdir -p "${TEST_BUILD_DIR}"
    cd "${TEST_BUILD_DIR}" || return 1

    cmake "${TEST_DIR}" -DCMAKE_PREFIX_PATH="${INSTALL_DIR};${YOMK_SERVER_PATH}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
    if [ $? -ne 0 ]; then
        echo "测试程序 cmake 配置失败"
        cd "${_ORIG_DIR}"
        return 1
    fi

    ${SUDO} cmake --build . --config Release --target install
    if [ $? -ne 0 ]; then
        echo "测试程序编译失败"
        cd "${_ORIG_DIR}"
        return 1
    fi
fi

cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成，扩展库已注册到系统动态库缓存，新开任意终端即可使用"
ldconfig -p | grep -i "${PROJECT_NAME}" || true
if [ "${BUILD_TEST}" = "ON" ]; then
    echo "测试程序已安装到 ${INSTALL_DIR}/bin，可直接运行 TestYomkExtension 验证"
fi
