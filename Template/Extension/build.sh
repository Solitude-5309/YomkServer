#!/bin/bash
# 用法: source build.sh [额外的cmake参数...]
# 示例: source build.sh -DCMAKE_PREFIX_PATH=/path/to/YomkServer/install

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="YomkMath"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"
TEST_DIR="${SCRIPT_DIR}/test"
TEST_BUILD_DIR="${TEST_DIR}/build"
_ORIG_DIR="$(pwd)"

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

cmake "${SCRIPT_DIR}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" "$@"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 编译测试程序
if [ "${BUILD_TEST}" = "ON" ]; then
    mkdir -p "${TEST_BUILD_DIR}"
    cd "${TEST_BUILD_DIR}" || return 1

    # 获取 YomkServer 路径
    YOMK_SERVER_PATH=""
    for arg in "$@"; do
        if [[ "${arg}" == -DCMAKE_PREFIX_PATH=* ]]; then
            YOMK_SERVER_PATH="${arg#-DCMAKE_PREFIX_PATH=}"
        fi
    done

    CMAKE_PREFIX_ARGS="-DCMAKE_PREFIX_PATH=${INSTALL_DIR}"
    if [ -n "${YOMK_SERVER_PATH}" ]; then
        CMAKE_PREFIX_ARGS="-DCMAKE_PREFIX_PATH=${INSTALL_DIR};${YOMK_SERVER_PATH}"
    fi

    cmake "${TEST_DIR}" ${CMAKE_PREFIX_ARGS}
    if [ $? -ne 0 ]; then
        echo "测试程序 cmake 配置失败"
        cd "${_ORIG_DIR}"
        return 1
    fi

    cmake --build . --config Release
    if [ $? -ne 0 ]; then
        echo "测试程序编译失败"
        cd "${_ORIG_DIR}"
        return 1
    fi

    # 设置临时环境变量
    YOMKSERVER_LIB_DIR=$(find "${YOMK_SERVER_PATH:-${INSTALL_DIR}/..}" -name "libYomkServer.so" 2>/dev/null | head -1 | xargs dirname 2>/dev/null)
    if [ -z "${YOMKSERVER_LIB_DIR}" ]; then
        YOMKSERVER_LIB_DIR="${INSTALL_DIR}/../YomkServer/install/lib"
    fi

    export LD_LIBRARY_PATH="${INSTALL_DIR}/lib:${YOMKSERVER_LIB_DIR}:${LD_LIBRARY_PATH}"
    export PATH="${TEST_BUILD_DIR}:${PATH}"
fi

cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成"
