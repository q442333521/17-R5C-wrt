#!/bin/bash
# =============================================================================
# 依赖库安装脚本
# 用于在 Ubuntu 22.04 ARM64 上安装 S7 和 OPC UA 库
# =============================================================================

set -e  # 遇到错误立即退出

echo "========================================="
echo "安装 S7 和 OPC UA 依赖库"
echo "========================================="
echo ""

# 检查是否为 root 用户
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要 root 权限,请使用 sudo 运行"
   exit 1
fi

# 更新包列表
echo "[1/4] 更新软件包列表..."
apt update

# 安装编译工具和基础依赖
echo ""
echo "[2/4] 安装编译工具和基础依赖..."
apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    libmodbus-dev \
    libjsoncpp-dev

# =============================================================================
# 安装 Snap7 (西门子 S7 通信库)
# =============================================================================
echo ""
echo "[3/4] 安装 Snap7 库..."

SNAP7_VERSION="1.4.2"
SNAP7_DIR="/tmp/snap7-$SNAP7_VERSION"

cd /tmp

# 下载 Snap7
if [ ! -f "snap7-full-$SNAP7_VERSION.tar.gz" ]; then
    echo "  下载 Snap7 $SNAP7_VERSION ..."
    wget https://sourceforge.net/projects/snap7/files/snap7-full-$SNAP7_VERSION.tar.gz
fi

# 解压
echo "  解压 Snap7..."
tar -xzf snap7-full-$SNAP7_VERSION.tar.gz

# 编译
cd $SNAP7_DIR/build/unix
echo "  编译 Snap7..."
make -f arm_v7_linux.mk

# 安装
echo "  安装 Snap7..."
cp -f $SNAP7_DIR/build/bin/arm_v7-linux/libsnap7.so /usr/local/lib/
cp -f $SNAP7_DIR/src/core/snap7.h /usr/local/include/

# 更新动态链接库缓存
ldconfig

# 验证安装
if [ -f /usr/local/lib/libsnap7.so ] && [ -f /usr/local/include/snap7.h ]; then
    echo "  ✅ Snap7 安装成功"
else
    echo "  ❌ Snap7 安装失败"
    exit 1
fi

# =============================================================================
# 安装 open62541 (OPC UA 通信库)
# =============================================================================
echo ""
echo "[4/4] 安装 open62541 库..."

OPEN62541_VERSION="1.3.5"
OPEN62541_DIR="/tmp/open62541-$OPEN62541_VERSION"

cd /tmp

# 下载 open62541
if [ ! -f "v$OPEN62541_VERSION.tar.gz" ]; then
    echo "  下载 open62541 $OPEN62541_VERSION ..."
    wget https://github.com/open62541/open62541/archive/refs/tags/v$OPEN62541_VERSION.tar.gz
fi

# 解压
echo "  解压 open62541..."
tar -xzf v$OPEN62541_VERSION.tar.gz

# 编译
cd $OPEN62541_DIR
mkdir -p build
cd build

echo "  配置 open62541..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DUA_ENABLE_AMALGAMATION=ON \
      ..

echo "  编译 open62541..."
make -j$(nproc)

# 安装
echo "  安装 open62541..."
make install

# 更新动态链接库缓存
ldconfig

# 验证安装
if [ -f /usr/local/lib/libopen62541.so ] && [ -f /usr/local/include/open62541/client.h ]; then
    echo "  ✅ open62541 安装成功"
else
    echo "  ❌ open62541 安装失败"
    exit 1
fi

# =============================================================================
# 清理临时文件
# =============================================================================
echo ""
echo "清理临时文件..."
cd /tmp
rm -rf snap7-* open62541-* v*.tar.gz

# =============================================================================
# 总结
# =============================================================================
echo ""
echo "========================================="
echo "✅ 所有依赖库安装完成"
echo "========================================="
echo ""
echo "已安装的库:"
echo "  • Snap7 $SNAP7_VERSION (S7 通信)"
echo "  • open62541 $OPEN62541_VERSION (OPC UA 通信)"
echo "  • libmodbus (Modbus 通信)"
echo "  • libjsoncpp (JSON 解析)"
echo ""
echo "现在可以运行编译脚本:"
echo "  ./scripts/build.sh"
echo ""
