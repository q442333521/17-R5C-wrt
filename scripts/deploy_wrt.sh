#!/bin/bash

################################################################################
# 部署到 FriendlyWRT 脚本（开发中）
# 
# 功能：
# 1. 交叉编译网关程序（静态链接）
# 2. 打包为 ipk 包或直接部署
# 3. 上传到 R5S 并安装
# 
# 使用方式：
#   ./scripts/deploy_wrt.sh [选项]
# 
# 选项：
#   build      - 仅编译（不部署）
#   package    - 编译并打包 ipk
#   deploy     - 编译、打包并部署到目标设备
#   install    - 在目标设备上安装（需先登录）
# 
# 注意：
# - 本脚本目前为模板，FriendlyWRT 移植尚未完成
# - 请参考 TODO.md 中的"FriendlyWRT 移植"章节
# 
# @author Gateway Project
# @date 2025-10-10
################################################################################

set -e  # 遇到错误立即退出

# ============================================================================
# 配置参数
# ============================================================================

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt"
TOOLCHAIN_FILE="$PROJECT_ROOT/toolchains/friendlywrt-aarch64.cmake"

# 目标设备 IP（根据实际情况修改）
TARGET_HOST="192.168.1.100"
TARGET_USER="root"

# FriendlyWRT 路径
TARGET_PREFIX="/opt/gw"
TARGET_BIN_DIR="$TARGET_PREFIX/bin"
TARGET_CONF_DIR="$TARGET_PREFIX/conf"
TARGET_INIT_DIR="/etc/init.d"

echo "========================================="
echo "FriendlyWRT 部署脚本"
echo "========================================="
echo "项目根目录: $PROJECT_ROOT"
echo "构建目录:   $BUILD_DIR"
echo "目标设备:   $TARGET_USER@$TARGET_HOST"
echo "========================================="
echo ""

# ============================================================================
# 检查前提条件
# ============================================================================

check_prerequisites() {
    echo "检查前提条件..."
    
    # 检查交叉编译工具链
    if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
        echo "错误: 交叉编译工具链未安装"
        echo "请运行: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
        exit 1
    fi
    
    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        echo "错误: CMake 未安装"
        exit 1
    fi
    
    # 检查工具链配置文件
    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        echo "警告: 工具链配置文件不存在: $TOOLCHAIN_FILE"
        echo "将创建模板文件..."
        create_toolchain_template
    fi
    
    echo "前提条件检查通过"
}

# ============================================================================
# 创建工具链配置文件模板
# ============================================================================

create_toolchain_template() {
    mkdir -p "$(dirname "$TOOLCHAIN_FILE")"
    
    cat > "$TOOLCHAIN_FILE" << 'EOF'
# CMake toolchain file for FriendlyWRT (aarch64)
# 
# 使用方式:
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/friendlywrt-aarch64.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 指定交叉编译工具
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# 指定 sysroot（FriendlyWRT SDK 路径，需要根据实际情况修改）
# set(CMAKE_SYSROOT /path/to/friendlywrt/sdk/staging_dir/target-aarch64_generic_musl)

# 查找库和头文件的路径
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 静态链接（减少依赖）
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

# 优化选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
EOF
    
    echo "工具链配置文件已创建: $TOOLCHAIN_FILE"
    echo "请根据实际情况修改 CMAKE_SYSROOT 路径"
}

# ============================================================================
# 交叉编译
# ============================================================================

build_for_wrt() {
    echo ""
    echo "========================================="
    echo "开始交叉编译..."
    echo "========================================="
    
    # 清理旧的构建目录
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 配置 CMake
    echo "配置 CMake..."
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$TARGET_PREFIX"
    
    # 编译
    echo "编译中..."
    make -j$(nproc)
    
    # 检查可执行文件
    if [ ! -f "src/rs485d/rs485d" ]; then
        echo "错误: 编译失败，找不到可执行文件"
        exit 1
    fi
    
    # 显示文件信息
    echo ""
    echo "编译完成！"
    echo "可执行文件:"
    file src/rs485d/rs485d
    file src/modbusd/modbusd
    file src/webcfg/webcfg
    
    # 显示大小
    echo ""
    echo "文件大小:"
    ls -lh src/rs485d/rs485d
    ls -lh src/modbusd/modbusd
    ls -lh src/webcfg/webcfg
}

# ============================================================================
# 打包为 ipk
# ============================================================================

package_ipk() {
    echo ""
    echo "========================================="
    echo "打包 ipk..."
    echo "========================================="
    
    # TODO: 实现 ipk 打包
    echo "警告: ipk 打包功能尚未实现"
    echo "请参考 OpenWrt 打包文档:"
    echo "  https://openwrt.org/docs/guide-developer/packages"
}

# ============================================================================
# 部署到目标设备
# ============================================================================

deploy_to_target() {
    echo ""
    echo "========================================="
    echo "部署到目标设备..."
    echo "========================================="
    
    # 检查 SSH 连接
    if ! ssh -o ConnectTimeout=5 "$TARGET_USER@$TARGET_HOST" "echo 'SSH 连接成功'" &> /dev/null; then
        echo "错误: 无法连接到目标设备 $TARGET_USER@$TARGET_HOST"
        echo "请检查:"
        echo "  1. 目标设备 IP 是否正确"
        echo "  2. SSH 服务是否运行"
        echo "  3. 防火墙设置"
        exit 1
    fi
    
    echo "创建目标目录..."
    ssh "$TARGET_USER@$TARGET_HOST" "mkdir -p $TARGET_BIN_DIR $TARGET_CONF_DIR"
    
    echo "上传可执行文件..."
    scp "$BUILD_DIR/src/rs485d/rs485d" "$TARGET_USER@$TARGET_HOST:$TARGET_BIN_DIR/"
    scp "$BUILD_DIR/src/modbusd/modbusd" "$TARGET_USER@$TARGET_HOST:$TARGET_BIN_DIR/"
    scp "$BUILD_DIR/src/webcfg/webcfg" "$TARGET_USER@$TARGET_HOST:$TARGET_BIN_DIR/"
    
    echo "设置执行权限..."
    ssh "$TARGET_USER@$TARGET_HOST" "chmod +x $TARGET_BIN_DIR/*"
    
    echo "上传配置文件..."
    scp "$PROJECT_ROOT/config/config.json" "$TARGET_USER@$TARGET_HOST:$TARGET_CONF_DIR/"
    
    # TODO: 上传 init.d 脚本（OpenWrt 格式）
    
    echo ""
    echo "========================================="
    echo "部署完成！"
    echo "========================================="
    echo "可执行文件位于: $TARGET_BIN_DIR"
    echo "配置文件位于:   $TARGET_CONF_DIR"
    echo ""
    echo "登录设备测试:"
    echo "  ssh $TARGET_USER@$TARGET_HOST"
    echo "  $TARGET_BIN_DIR/rs485d $TARGET_CONF_DIR/config.json"
}

# ============================================================================
# 在目标设备上安装服务
# ============================================================================

install_on_target() {
    echo ""
    echo "========================================="
    echo "在目标设备上安装服务..."
    echo "========================================="
    
    echo "TODO: 实现 procd init 脚本安装"
    echo "参考: https://openwrt.org/docs/techref/initscripts"
}

# ============================================================================
# 主流程
# ============================================================================

main() {
    local action="${1:-deploy}"
    
    case "$action" in
        build)
            check_prerequisites
            build_for_wrt
            ;;
        package)
            check_prerequisites
            build_for_wrt
            package_ipk
            ;;
        deploy)
            check_prerequisites
            build_for_wrt
            deploy_to_target
            ;;
        install)
            install_on_target
            ;;
        *)
            echo "用法: $0 {build|package|deploy|install}"
            echo ""
            echo "选项说明:"
            echo "  build   - 仅交叉编译"
            echo "  package - 编译并打包 ipk"
            echo "  deploy  - 编译并部署到目标设备"
            echo "  install - 在目标设备上安装服务"
            exit 1
            ;;
    esac
}

main "$@"

# ============================================================================
# 开发说明
# ============================================================================

# 本脚本为模板，FriendlyWRT 移植尚未完成。
# 
# 待完成任务（详见 TODO.md）：
# 
# 1. 搭建 FriendlyWRT SDK
#    - 下载 SDK: https://wiki.friendlyelec.com/wiki/index.php/NanoPi_R5S
#    - 安装交叉编译工具链
#    - 配置 CMake toolchain 文件
# 
# 2. 处理依赖库
#    - 交叉编译 libmodbus
#    - 交叉编译 jsoncpp
#    - 或使用 FriendlyWRT 提供的预编译库
# 
# 3. 适配 OpenWrt init 脚本
#    - 将 systemd 服务转换为 procd 格式
#    - 测试启动、停止、重启功能
# 
# 4. 制作 ipk 包
#    - 编写 Makefile
#    - 集成到 FriendlyWRT 构建系统
#    - 测试安装和卸载
# 
# 5. 固件集成
#    - 将网关集成到固件镜像
#    - 配置只读根文件系统
#    - 测试烧录和启动
# 
# 参考文档：
# - FriendlyWRT 文档: https://wiki.friendlyelec.com/
# - OpenWrt 开发文档: https://openwrt.org/docs/
# - 交叉编译指南: docs/CROSS_COMPILE.md（待创建）
