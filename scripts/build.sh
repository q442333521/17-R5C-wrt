#!/bin/bash

# Gateway Build Script
# 网关项目编译脚本

set -e  # 遇到错误立即退出

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
INSTALL_PREFIX="/opt/gw"

echo "========================================="
echo "Gateway Project Build Script"
echo "========================================="
echo "Project Root: $PROJECT_ROOT"
echo "Build Dir: $BUILD_DIR"
echo "Install Prefix: $INSTALL_PREFIX"
echo "========================================="

# 检查依赖
echo "Checking dependencies..."

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo "ERROR: $1 is not installed"
        echo "Please install it first:"
        echo "  sudo apt install $2"
        exit 1
    fi
}

check_command cmake cmake
check_command g++ build-essential
check_command pkg-config pkg-config

# 检查库
echo "Checking libraries..."

check_library() {
    if ! pkg-config --exists $1; then
        echo "ERROR: $1 library not found"
        echo "Please install it first:"
        echo "  sudo apt install $2"
        exit 1
    fi
}

check_library libmodbus libmodbus-dev
check_library jsoncpp libjsoncpp-dev

echo "All dependencies satisfied"

# 清理旧的构建目录（可选）
if [ "$1" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake
echo "========================================="
echo "Configuring CMake..."
echo "========================================="

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

# 编译
echo "========================================="
echo "Building..."
echo "========================================="

make -j$(nproc)

echo "========================================="
echo "Build completed successfully!"
echo "========================================="
echo ""
echo "Executables:"
ls -lh "$BUILD_DIR/src/rs485d/rs485d"
ls -lh "$BUILD_DIR/src/modbusd/modbusd"
ls -lh "$BUILD_DIR/src/webcfg/webcfg"
echo ""
echo "To install (requires sudo):"
echo "  sudo make install"
echo ""
echo "To run (for testing):"
echo "  $BUILD_DIR/src/rs485d/rs485d $PROJECT_ROOT/config/config.json"
echo "  $BUILD_DIR/src/modbusd/modbusd $PROJECT_ROOT/config/config.json"
echo "  $BUILD_DIR/src/webcfg/webcfg $PROJECT_ROOT/config/config.json"
echo ""
