#!/bin/bash
# =============================================================================
# 依赖库安装脚本 (更新版)
# 用于在 Ubuntu 22.04 ARM64 上安装 S7 和 OPC UA 库
# 
# 更新日期: 2025-10-11
# 版本: 2.1
# =============================================================================

set -e  # 遇到错误立即退出

echo "========================================="
echo "安装 S7 和 OPC UA 依赖库 (ARM64版)"
echo "========================================="
echo ""

# 检查是否为 root 用户
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要 root 权限,请使用 sudo 运行"
   exit 1
fi

# 检查架构
ARCH=$(uname -m)
echo "检测到架构: $ARCH"
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
    echo "警告: 此脚本专为 ARM64 架构设计,当前架构为 $ARCH"
    read -p "是否继续? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi
echo ""

# 更新包列表
echo "[1/5] 更新软件包列表..."
apt update

# 安装编译工具和基础依赖
echo ""
echo "[2/5] 安装编译工具和基础依赖..."
apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    p7zip-full \
    libmodbus-dev \
    libjsoncpp-dev \
    software-properties-common

# =============================================================================
# 安装 Snap7 (西门子 S7 通信库)
# =============================================================================
echo ""
echo "[3/5] 安装 Snap7 库..."

# 方案 A: 使用 PPA (推荐,最简单)
echo "  尝试从 PPA 安装..."
if add-apt-repository -y ppa:gijzelaar/snap7 2>/dev/null; then
    apt update
    if apt install -y libsnap7-1 libsnap7-dev 2>/dev/null; then
        echo "  ✅ Snap7 已从 PPA 安装"
    else
        echo "  ⚠️  PPA 安装失败,尝试从源码编译..."
        # 移除失败的 PPA
        add-apt-repository --remove -y ppa:gijzelaar/snap7 || true
        
        # 方案 B: 从源码编译
        SNAP7_VERSION="1.4.2"
        SNAP7_DIR="/tmp/snap7-$SNAP7_VERSION"
        
        cd /tmp
        
        # 下载 Snap7 ARM 版本 (直接链接)
        echo "  下载 Snap7 $SNAP7_VERSION (ARM版)..."
        if [ ! -f "snap7-iot-arm-$SNAP7_VERSION.tar.gz" ]; then
            wget -O snap7-iot-arm-$SNAP7_VERSION.tar.gz \
                "https://sourceforge.net/projects/snap7/files/iot/snap7-iot-arm-$SNAP7_VERSION.tar.gz/download"
        fi
        
        # 解压
        echo "  解压 Snap7..."
        tar -xzf snap7-iot-arm-$SNAP7_VERSION.tar.gz
        
        # 编译
        cd snap7-$SNAP7_VERSION/build/unix
        echo "  编译 Snap7..."
        make -f arm_v7_linux.mk all
        
        # 安装
        echo "  安装 Snap7..."
        install -m 0755 ../../bin/arm_v7-linux/libsnap7.so /usr/local/lib/libsnap7.so
        install -m 0644 ../../src/core/snap7.h /usr/local/include/snap7.h
        
        # 更新动态链接库缓存
        ldconfig
        
        echo "  ✅ Snap7 已从源码安装"
    fi
else
    echo "  ⚠️  无法添加 PPA,从源码编译..."
    
    # 方案 B: 从源码编译 (fallback)
    SNAP7_VERSION="1.4.2"
    SNAP7_DIR="/tmp/snap7-$SNAP7_VERSION"
    
    cd /tmp
    
    # 下载 Snap7 ARM 版本
    echo "  下载 Snap7 $SNAP7_VERSION (ARM版)..."
    if [ ! -f "snap7-iot-arm-$SNAP7_VERSION.tar.gz" ]; then
        wget -O snap7-iot-arm-$SNAP7_VERSION.tar.gz \
            "https://sourceforge.net/projects/snap7/files/iot/snap7-iot-arm-$SNAP7_VERSION.tar.gz/download"
    fi
    
    # 解压
    echo "  解压 Snap7..."
    tar -xzf snap7-iot-arm-$SNAP7_VERSION.tar.gz
    
    # 编译
    cd snap7-$SNAP7_VERSION/build/unix
    echo "  编译 Snap7..."
    make -f arm_v7_linux.mk all
    
    # 安装
    echo "  安装 Snap7..."
    install -m 0755 ../../bin/arm_v7-linux/libsnap7.so /usr/local/lib/libsnap7.so
    install -m 0644 ../../src/core/snap7.h /usr/local/include/snap7.h
    
    # 更新动态链接库缓存
    ldconfig
    
    echo "  ✅ Snap7 已从源码安装"
fi

# 验证安装
if [ -f /usr/lib/libsnap7.so ] || [ -f /usr/local/lib/libsnap7.so ]; then
    echo "  ✅ Snap7 库文件已就位"
else
    echo "  ❌ Snap7 安装失败"
    exit 1
fi

if [ -f /usr/include/snap7.h ] || [ -f /usr/local/include/snap7.h ]; then
    echo "  ✅ Snap7 头文件已就位"
else
    echo "  ❌ Snap7 头文件缺失"
    exit 1
fi

# =============================================================================
# 安装 open62541 (OPC UA 通信库)
# =============================================================================
echo ""
echo "[4/5] 安装 open62541 库..."

# 方案 A: 使用 PPA (推荐,最简单)
echo "  尝试从 PPA 安装..."
if add-apt-repository -y ppa:open62541-team/ppa 2>/dev/null; then
    apt update
    if apt install -y libopen62541-1-dev 2>/dev/null; then
        echo "  ✅ open62541 已从 PPA 安装"
    else
        echo "  ⚠️  PPA 安装失败,尝试从源码编译..."
        # 移除失败的 PPA
        add-apt-repository --remove -y ppa:open62541-team/ppa || true
        
        # 方案 B: 从源码编译
        OPEN62541_VERSION="1.3.14"
        OPEN62541_DIR="/tmp/open62541-$OPEN62541_VERSION"
        
        cd /tmp
        
        # 下载 open62541
        echo "  下载 open62541 $OPEN62541_VERSION ..."
        if [ ! -f "v$OPEN62541_VERSION.tar.gz" ]; then
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
              -DUA_NAMESPACE_ZERO=FULL \
              ..
        
        echo "  编译 open62541 (这可能需要几分钟)..."
        make -j$(nproc)
        
        # 安装
        echo "  安装 open62541..."
        make install
        
        # 更新动态链接库缓存
        ldconfig
        
        echo "  ✅ open62541 已从源码安装"
    fi
else
    echo "  ⚠️  无法添加 PPA,从源码编译..."
    
    # 方案 B: 从源码编译 (fallback)
    OPEN62541_VERSION="1.3.14"
    OPEN62541_DIR="/tmp/open62541-$OPEN62541_VERSION"
    
    cd /tmp
    
    # 下载 open62541
    echo "  下载 open62541 $OPEN62541_VERSION ..."
    if [ ! -f "v$OPEN62541_VERSION.tar.gz" ]; then
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
          -DUA_NAMESPACE_ZERO=FULL \
          ..
    
    echo "  编译 open62541 (这可能需要几分钟)..."
    make -j$(nproc)
    
    # 安装
    echo "  安装 open62541..."
    make install
    
    # 更新动态链接库缓存
    ldconfig
    
    echo "  ✅ open62541 已从源码安装"
fi

# 验证安装
if [ -f /usr/lib/libopen62541.so ] || [ -f /usr/local/lib/libopen62541.so ]; then
    echo "  ✅ open62541 库文件已就位"
else
    echo "  ❌ open62541 安装失败"
    exit 1
fi

if [ -f /usr/include/open62541/client.h ] || [ -f /usr/local/include/open62541/client.h ]; then
    echo "  ✅ open62541 头文件已就位"
else
    echo "  ❌ open62541 头文件缺失"
    exit 1
fi

# =============================================================================
# 清理临时文件
# =============================================================================
echo ""
echo "[5/5] 清理临时文件..."
cd /tmp
rm -rf snap7-* open62541-* v*.tar.gz || true

# =============================================================================
# 验证安装
# =============================================================================
echo ""
echo "========================================="
echo "✅ 验证安装"
echo "========================================="
echo ""

# 检查 Snap7
if ldconfig -p | grep -q libsnap7; then
    echo "✅ Snap7: $(ldconfig -p | grep libsnap7 | head -1 | awk '{print $NF}')"
else
    echo "❌ Snap7: 未找到库文件"
fi

# 检查 open62541
if ldconfig -p | grep -q libopen62541; then
    echo "✅ open62541: $(ldconfig -p | grep libopen62541 | head -1 | awk '{print $NF}')"
else
    echo "❌ open62541: 未找到库文件"
fi

# 检查头文件
echo ""
echo "头文件位置:"
find /usr/include /usr/local/include -name "snap7.h" 2>/dev/null | head -1 | xargs -I {} echo "  Snap7: {}"
find /usr/include /usr/local/include -name "client.h" -path "*/open62541/*" 2>/dev/null | head -1 | xargs -I {} echo "  open62541: {}"

# =============================================================================
# 总结
# =============================================================================
echo ""
echo "========================================="
echo "✅ 所有依赖库安装完成"
echo "========================================="
echo ""
echo "已安装的库:"
echo "  • Snap7 (S7 通信)"
echo "  • open62541 (OPC UA 通信)"
echo "  • libmodbus (Modbus 通信)"
echo "  • libjsoncpp (JSON 解析)"
echo ""
echo "下一步:"
echo "  1. 编译项目: ./scripts/build.sh"
echo "  2. 启动服务: ./scripts/start.sh"
echo ""
