#!/bin/bash
################################################################################
# 简单的 musl 工具链编译脚本
# 用途：编译可在 FriendlyWrt (musl libc) 上运行的二进制文件
################################################################################

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}musl 工具链编译脚本${NC}"
echo -e "${BLUE}=========================================${NC}"

# 获取项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt-musl"

echo -e "${GREEN}[INFO]${NC} 项目根目录: $PROJECT_ROOT"
echo -e "${GREEN}[INFO]${NC} 构建目录: $BUILD_DIR"
echo ""

# 检查 musl-gcc
if ! command -v musl-gcc &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} musl-gcc 未安装"
    echo -e "${YELLOW}[提示]${NC} 安装方法: sudo apt install musl-tools musl-dev"
    exit 1
fi

echo -e "${GREEN}[INFO]${NC} musl-gcc 版本:"
musl-gcc --version | head -1
echo ""

# 清理旧的构建目录
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}[INFO]${NC} 清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo -e "${GREEN}[INFO]${NC} 配置 CMake (使用 musl-gcc)..."
echo ""

# 配置 CMake
CC=musl-gcc \
CXX=musl-g++ \
cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=musl-gcc \
    -DCMAKE_CXX_COMPILER=musl-g++ \
    -DCMAKE_C_FLAGS="-O2 -march=armv8-a+crc -static" \
    -DCMAKE_CXX_FLAGS="-O2 -march=armv8-a+crc -static" \
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
    -DBUILD_SHARED_LIBS=OFF

echo ""
echo -e "${GREEN}[INFO]${NC} 开始编译（使用 $(nproc) 个核心）..."
echo ""

# 编译
make -j$(nproc)

echo ""
echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}编译完成！${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""

# 检查并显示编译产物
echo -e "${GREEN}[INFO]${NC} 编译产物:"
echo ""

for binary in rs485d modbusd webcfg; do
    bin_path="$BUILD_DIR/src/$binary/$binary"
    if [ -f "$bin_path" ]; then
        size=$(du -h "$bin_path" | cut -f1)
        echo -e "${GREEN}✓${NC} $binary"
        echo "  路径: $bin_path"
        echo "  大小: $size"
        
        # 检查文件类型
        file_info=$(file "$bin_path")
        echo "  类型: $file_info"
        
        # 检查是否静态链接
        if ldd "$bin_path" 2>&1 | grep -q "not a dynamic executable"; then
            echo -e "  链接: ${GREEN}静态链接 ✓${NC}"
        else
            echo -e "  链接: ${YELLOW}动态链接 (可能需要依赖)${NC}"
            ldd "$bin_path" 2>&1 | head -3 | sed 's/^/    /'
        fi
        echo ""
    else
        echo -e "${RED}✗${NC} $binary - 编译失败"
        echo ""
    fi
done

# 创建输出目录
OUTPUT_DIR="$PROJECT_ROOT/package/gw-gateway_musl"
echo -e "${GREEN}[INFO]${NC} 复制到输出目录: $OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/opt/gw/bin"
mkdir -p "$OUTPUT_DIR/opt/gw/conf"

# 复制二进制文件
cp "$BUILD_DIR/src/rs485d/rs485d" "$OUTPUT_DIR/opt/gw/bin/" 2>/dev/null || true
cp "$BUILD_DIR/src/modbusd/modbusd" "$OUTPUT_DIR/opt/gw/bin/" 2>/dev/null || true
cp "$BUILD_DIR/src/webcfg/webcfg" "$OUTPUT_DIR/opt/gw/bin/" 2>/dev/null || true
chmod +x "$OUTPUT_DIR/opt/gw/bin/"* 2>/dev/null || true

# 复制配置文件
if [ -f "$PROJECT_ROOT/config/config.json" ]; then
    cp "$PROJECT_ROOT/config/config.json" "$OUTPUT_DIR/opt/gw/conf/"
fi

echo ""
echo -e "${BLUE}=========================================${NC}"
echo -e "${GREEN}完成！${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""
echo -e "${YELLOW}[下一步]${NC} 将以下目录复制到 FriendlyWrt 设备："
echo "  $OUTPUT_DIR"
echo ""
echo -e "${YELLOW}[命令示例]${NC}"
echo "  # 在 FriendlyWrt 上执行："
echo "  mkdir -p /opt/gw"
echo "  # 然后复制 opt/gw/bin 和 opt/gw/conf 到 /opt/gw/"
echo ""
echo "  # 手动启动测试："
echo "  /opt/gw/bin/rs485d /opt/gw/conf/config.json &"
echo "  /opt/gw/bin/modbusd /opt/gw/conf/config.json &"
echo "  /opt/gw/bin/webcfg /opt/gw/conf/config.json &"
echo ""
