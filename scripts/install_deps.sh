#!/bin/bash
# =============================================================================
# 依赖库安装脚本 (修复版)
# 用于在 Ubuntu 22.04 ARM64 上安装 S7 和 OPC UA 库
# 
# 更新日期: 2025-10-11
# 版本: 2.3 (使用专用编译脚本)
# 
# 主要修改:
# - 修复 open62541-team/ppa 在 Ubuntu 22.04 不可用的问题
# - 使用专用的 build_open62541.sh 脚本编译 open62541
# - 添加了 PPA 清理功能
# - 模块化设计，更易维护
# =============================================================================

set -e  # 遇到错误立即退出

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
echo "[0/4] 清理有问题的 PPA 源..."

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
    p7zip-full \
    libmodbus-dev \
    libjsoncpp-dev \
    software-properties-common \
    python3 \
    python3-pip \
    lsb-release

# =============================================================================
# 安装 Snap7 (西门子 S7 通信库)
# =============================================================================
echo ""
echo "[3/4] 安装 Snap7 库..."

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
echo "[4/4] 安装 open62541 库..."

# 检查 Ubuntu 版本，决定安装策略
# open62541-team/ppa 只支持 Ubuntu 20.04 及以下，不支持 22.04 (jammy)
USE_SOURCE_BUILD=false

if [[ "$OS_VERSION" == "jammy" ]] || [[ "$OS_VERSION_NUM" == "22.04" ]]; then
    echo "  检测到 Ubuntu 22.04 (jammy)，open62541-team/ppa 不支持此版本"
    echo "  将使用专用脚本从源码编译 open62541..."
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

# 使用专用脚本从源码编译 open62541
if [ "$USE_SOURCE_BUILD" = true ]; then
    echo ""
    echo "  ==========================================  "
    echo "  使用专用脚本编译 open62541"
    echo "  =========================================="
    echo ""
    
    # 检查专用编译脚本是否存在
    BUILD_SCRIPT="$SCRIPT_DIR/build_open62541.sh"
    
    if [ ! -f "$BUILD_SCRIPT" ]; then
        echo "  ❌ 错误: 找不到编译脚本 $BUILD_SCRIPT"
        echo ""
        echo "  请确保 build_open62541.sh 与此脚本在同一目录下"
        exit 1
    fi
    
    # 给脚本添加执行权限
    chmod +x "$BUILD_SCRIPT"
    
    # 执行专用编译脚本 (使用 standard 配置 - 不含加密)
    # 如果需要加密支持，可以改为: BUILD_CONFIG=full
    echo "  调用专用编译脚本..."
    export BUILD_CONFIG=standard
    export CLEANUP_TEMP=1
    
    if bash "$BUILD_SCRIPT"; then
        echo ""
        echo "  ✅ open62541 编译安装成功"
    else
        echo ""
        echo "  ❌ open62541 编译安装失败"
        exit 1
    fi
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
echo "清理临时文件..."
cd /tmp
rm -rf snap7-* *.tar.gz *.deb 2>/dev/null || true
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
echo "  • open62541 (OPC UA 通信协议 - 标准配置)"
echo "  • libmodbus (Modbus TCP/RTU 通信)"
echo "  • libjsoncpp (JSON 数据解析)"
echo ""
echo "open62541 功能说明:"
echo "  ✅ 完整 OPC UA 客户端/服务器"
echo "  ✅ 订阅功能"
echo "  ✅ 方法调用"
echo "  ✅ 节点管理"
echo "  ❌ 加密支持 (如需加密，使用 BUILD_CONFIG=full 重新安装)"
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
echo "如果需要重新安装 open62541 的其他配置:"
echo "  sudo BUILD_CONFIG=minimal $SCRIPT_DIR/build_open62541.sh  # 最小化"
echo "  sudo BUILD_CONFIG=standard $SCRIPT_DIR/build_open62541.sh # 标准版 (当前)"
echo "  sudo BUILD_CONFIG=full $SCRIPT_DIR/build_open62541.sh     # 完整版 (含加密)"
echo ""
