#!/bin/bash
# =============================================================================
# open62541 专用编译安装脚本
# 用于在 Ubuntu 22.04 ARM64 上从源码编译 open62541 OPC UA 库
# 
# 创建日期: 2025-10-11
# 版本: 1.0
# 
# 功能说明:
# - 自动安装所有编译依赖（包括 MbedTLS）
# - 支持多种配置选项（标准版/精简版/完整版）
# - 详细的构建过程日志
# - 自动验证安装结果
# =============================================================================

set -e  # 遇到错误立即退出

# =============================================================================
# 配置参数
# =============================================================================

# open62541 版本号
OPEN62541_VERSION="1.3.14"

# 安装路径（可修改）
INSTALL_PREFIX="/usr/local"

# 构建目录
BUILD_DIR="/tmp/open62541-build-$$"  # 使用进程ID避免冲突

# 下载目录
DOWNLOAD_DIR="/tmp/open62541-download"

# 是否清理临时文件（1=清理，0=保留用于调试）
CLEANUP_TEMP=1

# 构建配置（可选择：minimal, standard, full）
# minimal  - 最小化配置，不含加密和高级功能
# standard - 标准配置，包含常用功能但不含加密
# full     - 完整配置，包含所有功能和加密支持
BUILD_CONFIG="standard"

# =============================================================================
# 颜色输出定义
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 输出函数
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_step() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

# =============================================================================
# 检查运行环境
# =============================================================================
print_step "步骤 1/7: 检查运行环境"

# 检查是否为 root 用户
if [[ $EUID -ne 0 ]]; then
   print_error "此脚本需要 root 权限，请使用 sudo 运行"
   exit 1
fi

# 检测操作系统
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_NAME=$NAME
    OS_VERSION=$VERSION_ID
    print_info "操作系统: $OS_NAME $OS_VERSION"
else
    print_error "无法检测操作系统版本"
    exit 1
fi

# 检查架构
ARCH=$(uname -m)
print_info "系统架构: $ARCH"

if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
    print_warning "此脚本针对 ARM64 架构优化，当前架构为 $ARCH"
fi

# 检查磁盘空间（需要至少 500MB）
AVAILABLE_SPACE=$(df /tmp | awk 'NR==2 {print $4}')
if [ $AVAILABLE_SPACE -lt 512000 ]; then
    print_error "磁盘空间不足，/tmp 需要至少 500MB 可用空间"
    exit 1
fi
print_info "可用磁盘空间: $(($AVAILABLE_SPACE / 1024))MB"

# 检查 CPU 核心数
CPU_CORES=$(nproc)
print_info "CPU 核心数: $CPU_CORES (将使用所有核心并行编译)"

# =============================================================================
# 安装编译依赖
# =============================================================================
print_step "步骤 2/7: 安装编译依赖"

print_info "更新软件包列表..."
apt update -qq

print_info "安装基础编译工具..."
apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    python3 \
    python3-pip \
    wget \
    ca-certificates

print_success "基础编译工具安装完成"

# 根据构建配置安装依赖
if [ "$BUILD_CONFIG" == "full" ]; then
    print_info "安装完整版依赖（包括加密库）..."
    
    # 安装 MbedTLS（用于加密支持）
    apt install -y libmbedtls-dev
    
    if dpkg -l | grep -q libmbedtls-dev; then
        MBEDTLS_VERSION=$(dpkg -l | grep libmbedtls-dev | awk '{print $3}')
        print_success "MbedTLS 已安装 (版本: $MBEDTLS_VERSION)"
    else
        print_error "MbedTLS 安装失败"
        exit 1
    fi
    
    # 安装其他可选依赖
    apt install -y libssl-dev libxml2-dev
    
elif [ "$BUILD_CONFIG" == "standard" ]; then
    print_info "安装标准版依赖（不含加密）..."
    # 标准版只需要基础工具，已在上面安装
    
else
    print_info "安装最小化版本依赖..."
    # 最小化版本只需要基础工具
fi

print_success "所有编译依赖安装完成"

# =============================================================================
# 下载 open62541 源码
# =============================================================================
print_step "步骤 3/7: 下载 open62541 源码"

# 创建下载目录
mkdir -p "$DOWNLOAD_DIR"
cd "$DOWNLOAD_DIR"

# 下载源码包
TARBALL="v${OPEN62541_VERSION}.tar.gz"
DOWNLOAD_URL="https://github.com/open62541/open62541/archive/refs/tags/${TARBALL}"

if [ -f "$TARBALL" ]; then
    print_info "源码包已存在，跳过下载"
else
    print_info "从 GitHub 下载 open62541 ${OPEN62541_VERSION}..."
    print_info "下载地址: $DOWNLOAD_URL"
    
    if wget --progress=bar:force "$DOWNLOAD_URL" 2>&1 | \
        grep --line-buffered "%" | \
        sed -u -e "s,\.,,g" | \
        awk '{printf("\r  下载进度: %s", $2)}'; then
        echo ""
        print_success "源码下载完成"
    else
        print_error "源码下载失败"
        exit 1
    fi
fi

# 验证下载文件
FILE_SIZE=$(stat -f%z "$TARBALL" 2>/dev/null || stat -c%s "$TARBALL")
print_info "文件大小: $(echo "scale=2; $FILE_SIZE/1024/1024" | bc)MB"

# =============================================================================
# 解压源码
# =============================================================================
print_step "步骤 4/7: 解压源码"

# 清理旧的构建目录
if [ -d "$BUILD_DIR" ]; then
    print_info "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

print_info "解压源码包..."
tar -xzf "$DOWNLOAD_DIR/$TARBALL" --strip-components=1

print_success "源码解压完成"

# =============================================================================
# 配置 CMake
# =============================================================================
print_step "步骤 5/7: 配置 CMake 构建选项"

# 创建构建目录
mkdir -p build
cd build

print_info "当前构建配置: $BUILD_CONFIG"

# 根据不同配置设置 CMake 参数
case "$BUILD_CONFIG" in
    "minimal")
        print_info "使用最小化配置（无加密，基础功能）"
        CMAKE_ARGS=(
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=ON
            -DUA_ENABLE_AMALGAMATION=OFF
            -DUA_NAMESPACE_ZERO=REDUCED
            -DUA_ENABLE_ENCRYPTION=OFF
            -DUA_ENABLE_SUBSCRIPTIONS=OFF
            -DUA_ENABLE_METHODCALLS=OFF
            -DUA_ENABLE_NODEMANAGEMENT=OFF
            -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
        )
        ;;
        
    "standard")
        print_info "使用标准配置（无加密，常用功能）"
        CMAKE_ARGS=(
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=ON
            -DUA_ENABLE_AMALGAMATION=OFF
            -DUA_NAMESPACE_ZERO=FULL
            -DUA_ENABLE_ENCRYPTION=OFF
            -DUA_ENABLE_SUBSCRIPTIONS=ON
            -DUA_ENABLE_METHODCALLS=ON
            -DUA_ENABLE_NODEMANAGEMENT=ON
            -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
        )
        ;;
        
    "full")
        print_info "使用完整配置（含加密，所有功能）"
        CMAKE_ARGS=(
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=ON
            -DUA_ENABLE_AMALGAMATION=OFF
            -DUA_NAMESPACE_ZERO=FULL
            -DUA_ENABLE_ENCRYPTION=MBEDTLS
            -DUA_ENABLE_SUBSCRIPTIONS=ON
            -DUA_ENABLE_METHODCALLS=ON
            -DUA_ENABLE_NODEMANAGEMENT=ON
            -DUA_ENABLE_PUBSUB=ON
            -DUA_ENABLE_PUBSUB_ETH_UADP=ON
            -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
        )
        ;;
        
    *)
        print_error "未知的构建配置: $BUILD_CONFIG"
        exit 1
        ;;
esac

# 显示详细的配置信息
print_info "CMake 配置参数:"
for arg in "${CMAKE_ARGS[@]}"; do
    echo "    $arg"
done

# 执行 CMake 配置
print_info "开始配置..."
if cmake "${CMAKE_ARGS[@]}" ..; then
    print_success "CMake 配置成功"
else
    print_error "CMake 配置失败，请查看错误信息"
    exit 1
fi

# =============================================================================
# 编译 open62541
# =============================================================================
print_step "步骤 6/7: 编译 open62541"

print_info "使用 $CPU_CORES 个 CPU 核心并行编译..."
print_warning "这可能需要 5-15 分钟，请耐心等待..."

# 记录开始时间
START_TIME=$(date +%s)

# 执行编译
if make -j$CPU_CORES; then
    # 计算编译时间
    END_TIME=$(date +%s)
    COMPILE_TIME=$((END_TIME - START_TIME))
    
    print_success "编译成功完成！"
    print_info "编译耗时: $COMPILE_TIME 秒"
else
    print_error "编译失败，请查看错误信息"
    exit 1
fi

# 显示编译产物
print_info "编译产物:"
if [ -f libopen62541.so ]; then
    LIB_SIZE=$(stat -f%z libopen62541.so 2>/dev/null || stat -c%s libopen62541.so)
    print_info "  libopen62541.so ($(echo "scale=2; $LIB_SIZE/1024/1024" | bc)MB)"
fi

# =============================================================================
# 安装到系统
# =============================================================================
print_step "步骤 7/7: 安装到系统"

print_info "安装位置: $INSTALL_PREFIX"
print_info "  库文件: $INSTALL_PREFIX/lib/"
print_info "  头文件: $INSTALL_PREFIX/include/open62541/"
print_info "  CMake配置: $INSTALL_PREFIX/lib/cmake/open62541/"

# 执行安装
if make install; then
    print_success "安装成功"
else
    print_error "安装失败"
    exit 1
fi

# 更新动态链接库缓存
print_info "更新动态链接库缓存..."
ldconfig

# =============================================================================
# 验证安装
# =============================================================================
print_step "验证安装"

# 检查库文件
print_info "检查库文件..."
if [ -f "$INSTALL_PREFIX/lib/libopen62541.so" ]; then
    print_success "库文件: $INSTALL_PREFIX/lib/libopen62541.so"
    
    # 显示库文件详细信息
    LIB_INFO=$(file "$INSTALL_PREFIX/lib/libopen62541.so")
    print_info "  $LIB_INFO"
else
    print_error "库文件未找到"
    exit 1
fi

# 检查头文件
print_info "检查头文件..."
HEADER_COUNT=$(find "$INSTALL_PREFIX/include/open62541" -name "*.h" 2>/dev/null | wc -l)
if [ $HEADER_COUNT -gt 0 ]; then
    print_success "头文件: $HEADER_COUNT 个头文件"
else
    # 检查是否是 amalgamation 版本
    if [ -f "$INSTALL_PREFIX/include/open62541.h" ]; then
        print_success "头文件: $INSTALL_PREFIX/include/open62541.h (amalgamation版本)"
    else
        print_error "头文件未找到"
        exit 1
    fi
fi

# 检查 pkg-config
print_info "检查 pkg-config..."
if pkg-config --exists open62541 2>/dev/null; then
    VERSION=$(pkg-config --modversion open62541)
    CFLAGS=$(pkg-config --cflags open62541)
    LIBS=$(pkg-config --libs open62541)
    
    print_success "pkg-config 配置正确"
    print_info "  版本: $VERSION"
    print_info "  编译参数: $CFLAGS"
    print_info "  链接参数: $LIBS"
else
    print_warning "pkg-config 未找到 open62541，但库文件已安装"
fi

# 检查动态链接库
print_info "检查动态链接库缓存..."
if ldconfig -p | grep -q libopen62541; then
    LIB_PATH=$(ldconfig -p | grep libopen62541 | head -1 | awk '{print $NF}')
    print_success "已注册到系统: $LIB_PATH"
else
    print_error "未在动态链接库缓存中找到"
    exit 1
fi

# =============================================================================
# 清理临时文件
# =============================================================================
if [ $CLEANUP_TEMP -eq 1 ]; then
    print_step "清理临时文件"
    
    print_info "清理构建目录: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    
    print_info "保留源码包: $DOWNLOAD_DIR/$TARBALL"
    
    print_success "清理完成"
else
    print_step "保留临时文件（用于调试）"
    print_info "构建目录: $BUILD_DIR"
    print_info "源码包: $DOWNLOAD_DIR/$TARBALL"
fi

# =============================================================================
# 输出使用说明
# =============================================================================
print_step "安装完成！"

echo ""
print_success "open62541 ${OPEN62541_VERSION} 已成功安装"
echo ""
print_info "构建配置: $BUILD_CONFIG"
print_info "安装位置: $INSTALL_PREFIX"
echo ""

# 根据不同配置给出使用建议
case "$BUILD_CONFIG" in
    "minimal")
        print_info "已安装功能:"
        echo "  ✅ 基础 OPC UA 客户端/服务器"
        echo "  ❌ 加密支持"
        echo "  ❌ 订阅功能"
        echo "  ❌ 方法调用"
        ;;
        
    "standard")
        print_info "已安装功能:"
        echo "  ✅ 完整 OPC UA 客户端/服务器"
        echo "  ✅ 订阅功能"
        echo "  ✅ 方法调用"
        echo "  ✅ 节点管理"
        echo "  ❌ 加密支持（如需加密，使用 full 配置重新编译）"
        ;;
        
    "full")
        print_info "已安装功能:"
        echo "  ✅ 完整 OPC UA 客户端/服务器"
        echo "  ✅ MbedTLS 加密支持"
        echo "  ✅ 订阅功能"
        echo "  ✅ 方法调用"
        echo "  ✅ 节点管理"
        echo "  ✅ PubSub 支持"
        ;;
esac

echo ""
print_info "在 CMakeLists.txt 中使用方法:"
echo ""
echo "  # 方法 1: 使用 find_package（推荐）"
echo "  find_package(open62541 REQUIRED)"
echo "  target_link_libraries(你的目标 open62541::open62541)"
echo ""
echo "  # 方法 2: 直接链接"
echo "  target_include_directories(你的目标 PRIVATE $INSTALL_PREFIX/include)"
echo "  target_link_libraries(你的目标 open62541)"
echo ""

print_info "测试安装:"
echo "  pkg-config --modversion open62541"
echo "  pkg-config --cflags open62541"
echo "  pkg-config --libs open62541"
echo ""

print_info "如需重新编译其他配置:"
echo "  sudo BUILD_CONFIG=minimal ./build_open62541.sh"
echo "  sudo BUILD_CONFIG=standard ./build_open62541.sh"
echo "  sudo BUILD_CONFIG=full ./build_open62541.sh"
echo ""

print_success "脚本执行完成！"
