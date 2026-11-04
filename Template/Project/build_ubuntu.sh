#!/bin/bash
# 一键编译脚本（交互式）
# 用法: source build_ubuntu.sh（也兼容直接执行 ./build_ubuntu.sh，两种方式失败均正确退出）
# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH，可修改
# 安装路径固定为工程源码目录下的 install/（CMakeLists.txt 已强制）

# 兼容直接执行：脚本内部以 return 表达失败（source 语义），直接执行时 return 在顶层非法
# 且不会终止脚本（失败后继续打印成功横幅）——转交 source 方式运行并透传退出码
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    bash -c "source '${BASH_SOURCE[0]}'"
    exit $?
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
_ORIG_DIR="$(pwd)"

# 交互询问 YomkServer 安装路径（前置路径），默认取环境变量 YOMK_PREFIX_PATH
if [ -z "${YOMK_PREFIX_PATH}" ]; then
    echo "警告: 未检测到环境变量 YOMK_PREFIX_PATH，可能未通过 build_ubuntu.sh 安装 YomkServer，请手动输入安装路径"
fi
read -r -p "请输入 YomkServer 安装路径 [默认: ${YOMK_PREFIX_PATH:-无}]: " _INPUT_PREFIX
YOMK_SERVER_PATH="${_INPUT_PREFIX:-${YOMK_PREFIX_PATH}}"
unset _INPUT_PREFIX

# 展开路径开头的 ~
YOMK_SERVER_PATH="${YOMK_SERVER_PATH/#\~/$HOME}"
# 非绝对路径自动补全为基于当前目录的绝对路径
if [[ -n "${YOMK_SERVER_PATH}" && "${YOMK_SERVER_PATH}" != /* ]]; then
    YOMK_SERVER_PATH="$(pwd)/${YOMK_SERVER_PATH}"
fi

if [ -z "${YOMK_SERVER_PATH}" ]; then
    echo "错误: 未指定 YomkServer 安装路径"
    return 1
fi
echo "-- YomkServer 安装路径: ${YOMK_SERVER_PATH}"

# 创建并进入 build 目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

# 配置
cmake "${SCRIPT_DIR}" -DCMAKE_PREFIX_PATH="${YOMK_SERVER_PATH}"
if [ $? -ne 0 ]; then
    echo "cmake 配置失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 编译并安装
cmake --build . --config Release --target install
if [ $? -ne 0 ]; then
    echo "编译失败"
    cd "${_ORIG_DIR}"
    return 1
fi

# 加载运行环境
source "${SCRIPT_DIR}/install/setup.bash"

# 恢复原始目录
cd "${_ORIG_DIR}"
unset _ORIG_DIR

echo "编译完成"
