#!/bin/bash
# =============================================================================
# 依赖库安装脚本 (修复版)
# 用于在 Ubuntu 22.04 ARM64 上安装 S7 和 OPC UA 库
# 
# 更新日期: 2025-10-11
# 版本: 2.2 (修复 open62541 PPA 404 错误)
# 
# 主要修改:
# - 修复 open62541-team/ppa 在 Ubuntu 22.04 不可用的问题
# - 对于 Ubuntu 22.04，直接使用源码编译 open62541
# - 添加了 PPA 清理功能
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

# 检测操作系统版本
OS_VERSION=$(lsb_release -cs 2>/dev/null || echo "unknown")
OS_VERSION_NUM=$(lsb_release -rs 2>/dev/null || echo "0")
echo "检测到操作系统版本: Ubuntu $OS_VERSION_NUM ($OS_VERSION)"

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

# =============================================================================
# 清理可能存在的有问题的 PPA 源
# =============================================================================
echo "[0/5] 清理有问题的 PPA 源..."

# 清理 open62541-team PPA (Ubuntu 22.04 不支持)
if [ -f /etc/apt/sources.list.d/open62541-team-ubuntu-ppa-${OS_VERSION}.list ]; then
    echo "  移除 open62541-team PPA 配置文件..."
    rm -f /etc/apt/sources.list.d/open62541-team-ubuntu-ppa-${OS_VERSION}.list
fi

if [ -f /etc/apt/trusted.gpg.d/open62541-team-ubuntu-ppa.gpg ]; then
    echo "  移除 open62541-team PPA GPG 密钥..."
    rm -f /etc/apt/trusted.gpg.d/open62541-team-ubuntu-ppa.gpg
fi

# 尝试使用 add-apt-repository 移除（如果存在）
add-apt-repository --remove -y ppa:open62541-team/ppa 2>/dev/null || true

echo "  ✅ PPA 清理完成"
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
    software-properties-common \
    python3 \
    python3-pip

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

# 检查 Ubuntu 版本，决定安装策略
# open62541-team/ppa 只支持 Ubuntu 20.04 及以下，不支持 22.04 (jammy)
USE_SOURCE_BUILD=false

if [[ "$OS_VERSION" == "jammy" ]] || [[ "$OS_VERSION_NUM" == "22.04" ]]; then
    echo "  检测到 Ubuntu 22.04 (jammy)，open62541-team/ppa 不支持此版本"
    echo "  将直接从源码编译 open62541..."
    USE_SOURCE_BUILD=true
elif [[ "$OS_VERSION" == "focal" ]] || [[ "$OS_VERSION_NUM" == "20.04" ]]; then
    echo "  检测到 Ubuntu 20.04 (focal)，尝试使用 PPA..."
    USE_SOURCE_BUILD=false
else
    echo "  检测到未知版本，将尝试 PPA，失败则从源码编译..."
    USE_SOURCE_BUILD=false
fi

# 根据系统版本选择安装方式
if [ "$USE_SOURCE_BUILD" = false ]; then
    # 尝试 PPA 安装
    echo "  尝试从 PPA 安装..."
    if add-apt-repository -y ppa:open62541-team/ppa 2>/dev/null; then
        apt update
        if apt install -y libopen62541-1-dev 2>/dev/null; then
            echo "  ✅ open62541 已从 PPA 安装"
        else
            echo "  ⚠️  PPA 安装失败,切换到源码编译..."
            add-apt-repository --remove -y ppa:open62541-team/ppa || true
            USE_SOURCE_BUILD=true
        fi
    else
        echo "  ⚠️  无法添加 PPA,切换到源码编译..."
        USE_SOURCE_BUILD=true
    fi
fi

# 从源码编译 open62541
if [ "$USE_SOURCE_BUILD" = true ]; then
    echo "  开始从源码编译 open62541..."
    
    # 设置版本号（可以根据需要修改）
    OPEN62541_VERSION="1.3.14"
    OPEN62541_DIR="/tmp/open62541-$OPEN62541_VERSION"
    
    cd /tmp
    
    # 下载 open62541 源码
    echo "  下载 open62541 $OPEN62541_VERSION 源码..."
    if [ ! -f "v$OPEN62541_VERSION.tar.gz" ]; then
        # 从 GitHub 下载指定版本
        wget https://github.com/open62541/open62541/archive/refs/tags/v$OPEN62541_VERSION.tar.gz
    fi
    
    # 解压源码
    echo "  解压 open62541 源码..."
    if [ -d "$OPEN62541_DIR" ]; then
        rm -rf "$OPEN62541_DIR"
    fi
    tar -xzf v$OPEN62541_VERSION.tar.gz
    
    # 进入源码目录并创建构建目录
    cd $OPEN62541_DIR
    mkdir -p build
    cd build
    
    # 配置 CMake 参数
    echo "  配置 CMake 构建选项..."
    echo "    - 编译类型: Release (优化版本)"
    echo "    - 共享库: 启用"
    echo "    - 命名空间: FULL (完整 OPC UA 功能)"
    echo "    - 加密: 启用"
    echo "    - 订阅: 启用"
    echo "    - 方法调用: 启用"
    
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          -DUA_ENABLE_AMALGAMATION=OFF \
          -DUA_NAMESPACE_ZERO=FULL \
          -DUA_ENABLE_ENCRYPTION=ON \
          -DUA_ENABLE_SUBSCRIPTIONS=ON \
          -DUA_ENABLE_METHODCALLS=ON \
          -DUA_ENABLE_NODEMANAGEMENT=ON \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          ..
    
    # 编译（使用所有可用 CPU 核心）
    CPU_CORES=$(nproc)
    echo "  开始编译 open62541 (使用 $CPU_CORES 个 CPU 核心，这可能需要 5-15 分钟)..."
    make -j$CPU_CORES
    
    # 安装到系统
    echo "  安装 open62541 到 /usr/local..."
    make install
    
    # 更新动态链接库缓存
    echo "  更新动态链接库缓存..."
    ldconfig
    
    echo "  ✅ open62541 已从源码成功编译并安装"
    
    # 显示安装信息
    echo ""
    echo "  open62541 安装位置:"
    echo "    库文件: /usr/local/lib/libopen62541.so"
    echo "    头文件: /usr/local/include/open62541/"
    echo "    CMake配置: /usr/local/lib/cmake/open62541/"
fi

# 验证安装
echo ""
echo "  验证 open62541 安装..."
if [ -f /usr/lib/libopen62541.so ] || [ -f /usr/local/lib/libopen62541.so ]; then
    echo "  ✅ open62541 库文件已就位"
else
    echo "  ❌ open62541 库文件未找到"
    exit 1
fi

if [ -f /usr/include/open62541/client.h ] || [ -f /usr/local/include/open62541/client.h ]; then
    echo "  ✅ open62541 头文件已就位"
elif [ -f /usr/local/include/open62541.h ]; then
    echo "  ✅ open62541 头文件已就位 (amalgamation版本)"
else
    echo "  ❌ open62541 头文件缺失"
    exit 1
fi

# 检查 pkg-config 是否能找到 open62541
if pkg-config --exists open62541 2>/dev/null; then
    OPEN62541_VERSION_INSTALLED=$(pkg-config --modversion open62541)
    echo "  ✅ open62541 已正确注册到 pkg-config (版本: $OPEN62541_VERSION_INSTALLED)"
else
    echo "  ⚠️  警告: pkg-config 无法找到 open62541，但库文件已安装"
fi

# =============================================================================
# 清理临时文件
# =============================================================================
echo ""
echo "[5/5] 清理临时文件..."
cd /tmp
rm -rf snap7-* open62541-* v*.tar.gz *.deb || true
echo "  ✅ 临时文件清理完成"

# =============================================================================
# 验证安装
# =============================================================================
echo ""
echo "========================================="
echo "✅ 验证安装"
echo "========================================="
echo ""

# 检查 Snap7
echo "Snap7 库:"
if ldconfig -p | grep -q libsnap7; then
    SNAP7_LIB=$(ldconfig -p | grep libsnap7 | head -1 | awk '{print $NF}')
    echo "  ✅ 库文件: $SNAP7_LIB"
else
    echo "  ❌ 未找到库文件"
fi

SNAP7_HEADER=$(find /usr/include /usr/local/include -name "snap7.h" 2>/dev/null | head -1)
if [ -n "$SNAP7_HEADER" ]; then
    echo "  ✅ 头文件: $SNAP7_HEADER"
else
    echo "  ❌ 未找到头文件"
fi

echo ""

# 检查 open62541
echo "open62541 库:"
if ldconfig -p | grep -q libopen62541; then
    OPEN62541_LIB=$(ldconfig -p | grep libopen62541 | head -1 | awk '{print $NF}')
    echo "  ✅ 库文件: $OPEN62541_LIB"
else
    echo "  ❌ 未找到库文件"
fi

OPEN62541_HEADER=$(find /usr/include /usr/local/include -name "client.h" -path "*/open62541/*" 2>/dev/null | head -1)
if [ -z "$OPEN62541_HEADER" ]; then
    # 如果找不到，尝试查找 amalgamation 版本
    OPEN62541_HEADER=$(find /usr/include /usr/local/include -name "open62541.h" 2>/dev/null | head -1)
fi

if [ -n "$OPEN62541_HEADER" ]; then
    echo "  ✅ 头文件: $OPEN62541_HEADER"
else
    echo "  ❌ 未找到头文件"
fi

echo ""

# 检查其他依赖
echo "其他依赖库:"
if ldconfig -p | grep -q libmodbus; then
    echo "  ✅ libmodbus"
else
    echo "  ⚠️  libmodbus"
fi

if ldconfig -p | grep -q libjsoncpp; then
    echo "  ✅ libjsoncpp"
else
    echo "  ⚠️  libjsoncpp"
fi

# =============================================================================
# 总结
# =============================================================================
echo ""
echo "========================================="
echo "✅ 所有依赖库安装完成"
echo "========================================="
echo ""
echo "已安装的库:"
echo "  • Snap7 (西门子 S7 通信协议)"
echo "  • open62541 (OPC UA 通信协议)"
echo "  • libmodbus (Modbus TCP/RTU 通信)"
echo "  • libjsoncpp (JSON 数据解析)"
echo ""
echo "在 CMakeLists.txt 中使用方法:"
echo ""
echo "  # 查找 open62541"
echo "  find_package(open62541 REQUIRED)"
echo "  target_link_libraries(你的目标 open62541::open62541)"
echo ""
echo "  # 或者直接链接"
echo "  target_include_directories(你的目标 PRIVATE /usr/local/include)"
echo "  target_link_libraries(你的目标 open62541 snap7 modbus jsoncpp)"
echo ""
echo "下一步操作:"
echo "  1. 配置项目: cd /path/to/project && mkdir build && cd build"
echo "  2. 生成构建文件: cmake .."
echo "  3. 编译项目: make -j$(nproc)"
echo ""
echo "如果遇到问题，请检查:"
echo "  • CMake 能否找到库: pkg-config --list-all | grep open62541"
echo "  • 库文件路径: ldconfig -p | grep 'open62541\\|snap7'"
echo "  • 环境变量: echo \$PKG_CONFIG_PATH"
echo ""
