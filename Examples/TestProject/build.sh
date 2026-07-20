#!/bin/bash
# 一键编译脚本
# 用法: source build.sh [额外的cmake参数...]
# 示例: source build.sh -DCMAKE_PREFIX_PATH=/path/to/YomkServer/install

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
_ORIG_DIR="$(pwd)"

# 创建并进入 build 目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || return 1

# 配置
cmake "${SCRIPT_DIR}" "$@"
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
