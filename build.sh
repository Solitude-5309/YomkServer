#!/bin/bash
set -e

# ========== 颜色 ==========
GREEN="\033[32m"
YELLOW="\033[33m"
NC="\033[0m"

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }

# ========== 环境检查 ==========
check_and_install_deps() {
    local APT_DEPS=(
        gcc
        swig
        python3-dev
        build-essential
        cmake
    )

    local PIP_DEPS=(
        cython
        readerwriterlock
    )

    local MISSING=()

    for pkg in "${APT_DEPS[@]}"; do
        if ! dpkg -l | grep -q "^ii  $pkg "; then
            MISSING+=("$pkg")
        fi
    done

    if [ ${#MISSING[@]} -ne 0 ]; then
        warn "检测到以下 APT 依赖未安装：${MISSING[*]}"
        read -p "是否现在安装这些依赖？(y/n，默认 y): " ANSWER
        ANSWER=${ANSWER:-y}
        if [[ "$ANSWER" =~ ^[Yy]$ ]]; then
            info "开始安装 APT 依赖..."
            sudo apt update
            sudo apt install -y "${MISSING[@]}"
        else
            warn "依赖不完整，脚本终止"
            exit 1
        fi
    else
        info "所有 APT 依赖已安装，跳过"
    fi

    # Python pip 依赖
    for pkg in "${PIP_DEPS[@]}"; do
        if ! python3 -c "import $pkg" &>/dev/null; then
            warn "Python 依赖 $pkg 未安装"
            read -p "是否现在 pip install $pkg？(y/n，默认 y): " ANSWER
            ANSWER=${ANSWER:-y}
            if [[ "$ANSWER" =~ ^[Yy]$ ]]; then
                pip install "$pkg"
            else
                warn "Python 依赖不完整，脚本终止"
                exit 1
            fi
        fi
    done

    info "所有依赖检查完成"
}

# ========== 安装前缀 ==========
read -p "请输入 YomkServerPy 安装目录（回车使用 ~/YomkServer/install）: " INSTALL_DIR

if [ -z "$INSTALL_DIR" ]; then
    INSTALL_DIR="$HOME/YomkServer/install"
    warn "未输入路径，使用默认目录: $INSTALL_DIR"
fi

INSTALL_DIR="${INSTALL_DIR/#\~/$HOME}"

if [ ! -d "$INSTALL_DIR" ]; then
    mkdir -p "$INSTALL_DIR"
fi

INSTALL_DIR=$(realpath "$INSTALL_DIR")

# ========== 编译类型 ==========
read -p "选择编译类型 (R)elease / (D)ebug（默认 Release）: " BUILD_TYPE

case "$BUILD_TYPE" in
    [Dd]*)
        BUILD_TYPE="Debug"
        ;;
    *)
        BUILD_TYPE="Release"
        ;;
esac

info "编译类型: $BUILD_TYPE"

# ========== 并行编译核数 ==========
JOBS=$(nproc)
info "并行编译核数: $JOBS"

# ========== 编译函数 ==========
build_project() {
    local NAME=$1
    local SRC_DIR=$2

    info "开始编译 $NAME ($BUILD_TYPE)"

    rm -rf "$SRC_DIR/build"
    mkdir -p "$SRC_DIR/build"
    cd "$SRC_DIR/build"

    cmake .. \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

    cmake --build . \
        --config "$BUILD_TYPE" \
        -j"$JOBS"

    # ✅ 直接 install，失败就 warn，不中断脚本
    cmake --build . \
        --target install \
        --config "$BUILD_TYPE" \
        || warn "$NAME install 失败，跳过安装"

    cd -
}

# ========== 主流程 ==========
check_and_install_deps

ROOT_DIR=$(pwd)
info "安装目录: $INSTALL_DIR"

# YomkServerPy
build_project "YomkServerPy" "$ROOT_DIR"

info "✅ YomkServerPy 编译完成"
info "Install prefix: $INSTALL_DIR"
info "Build type: $BUILD_TYPE"