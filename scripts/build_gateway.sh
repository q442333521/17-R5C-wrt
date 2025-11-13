#!/bin/bash
# Gateway Bridge 构建脚本

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-gateway"

echo "═══════════════════════════════════════════"
echo "  Gateway Bridge 构建脚本"
echo "═══════════════════════════════════════════"
echo ""

# 检查依赖
echo "检查依赖库..."
check_dependency() {
    if ! pkg-config --exists "$1"; then
        echo "错误: 未找到 $1"
        echo "请运行: sudo apt install $2"
        exit 1
    fi
    echo "  ✓ $1 $(pkg-config --modversion $1)"
}

check_dependency "libmodbus" "libmodbus-dev"
check_dependency "jsoncpp" "libjsoncpp-dev"

# 检查Snap7
if [ ! -f "/usr/local/lib/libsnap7.so" ]; then
    echo "错误: 未找到 Snap7 库"
    echo "请参考文档安装 Snap7"
    exit 1
fi
echo "  ✓ Snap7"

# 创建构建目录
echo ""
echo "创建构建目录..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# CMake配置
echo ""
echo "运行 CMake..."
cmake "${PROJECT_ROOT}/src/gateway-bridge" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

# 编译
echo ""
echo "编译中..."
make -j$(nproc)

# 检查结果
if [ -f "gateway-bridge" ]; then
    echo ""
    echo "═══════════════════════════════════════════"
    echo "  ✓ 编译成功！"
    echo "═══════════════════════════════════════════"
    echo ""
    echo "可执行文件: ${BUILD_DIR}/gateway-bridge"
    echo ""
    echo "下一步:"
    echo "  1. 测试: ./gateway-bridge --config ../config/gateway_config.json"
    echo "  2. 安装: sudo make install"
    echo ""
else
    echo "编译失败！"
    exit 1
fi
