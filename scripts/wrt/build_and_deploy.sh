#!/bin/bash

################################################################################
# FriendlyWrt R5S 本地编译和部署脚本
# 
# 功能：
# 1. 在 Ubuntu 22.04 ARM64 上本地编译（无需交叉编译）
# 2. 制作 IPK 安装包
# 3. 部署到 FriendlyWrt R5S 设备
# 4. 配置和启动服务
#
# 使用方式：
#   ./scripts/wrt/build_and_deploy.sh [命令]
#
# 命令：
#   build      - 仅编译
#   package    - 编译并制作 IPK 包
#   deploy     - 编译、打包并部署到设备
#   install    - 仅安装已有的 IPK 包到设备
#   test       - 在设备上运行测试
#
# @author Gateway Project
# @date 2025-10-13
################################################################################

set -e  # 遇到错误立即退出

# ============================================================================
# 配置参数
# ============================================================================

# 项目配置
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt"
PACKAGE_DIR="$PROJECT_ROOT/package"
IPK_VERSION="1.0.0"

# FriendlyWrt 设备配置（根据你的 SSH 配置）
DEVICE_HOST="100.121.179.13"
DEVICE_USER="root"
DEVICE_NAME="FriendlyWrt"

# 目标路径（在 FriendlyWrt 上）
TARGET_PREFIX="/opt/gw"
TARGET_BIN_DIR="$TARGET_PREFIX/bin"
TARGET_CONF_DIR="$TARGET_PREFIX/conf"
TARGET_LOG_DIR="$TARGET_PREFIX/logs"
TARGET_DATA_DIR="$TARGET_PREFIX/data"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# 辅助函数
# ============================================================================

print_header() {
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=========================================${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ============================================================================
# 检查前提条件
# ============================================================================

check_prerequisites() {
    print_header "检查前提条件"
    
    # 检查必要的工具
    local tools=("cmake" "make" "gcc" "g++" "ssh" "scp")
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            print_error "$tool 未安装"
            exit 1
        fi
    done
    
    # 检查依赖库
    print_info "检查依赖库..."
    if ! pkg-config --exists libmodbus; then
        print_error "libmodbus 未安装"
        print_info "请运行: sudo apt install libmodbus-dev"
        exit 1
    fi
    
    if ! pkg-config --exists jsoncpp; then
        print_error "jsoncpp 未安装"
        print_info "请运行: sudo apt install libjsoncpp-dev"
        exit 1
    fi
    
    # 检查设备连接
    print_info "检查设备连接..."
    if ! ssh -o ConnectTimeout=5 "$DEVICE_USER@$DEVICE_HOST" "echo 'SSH 连接成功'" &> /dev/null; then
        print_warning "无法连接到 $DEVICE_NAME ($DEVICE_HOST)"
        print_warning "部署功能将不可用,但可以继续编译"
    else
        print_info "设备连接正常: $DEVICE_USER@$DEVICE_HOST"
    fi
    
    print_info "前提条件检查完成 ✓"
    echo ""
}

# ============================================================================
# 编译项目
# ============================================================================

build_project() {
    print_header "开始编译项目"
    
    # 清理旧的构建目录
    if [ -d "$BUILD_DIR" ]; then
        print_info "清理旧的构建目录..."
        rm -rf "$BUILD_DIR"
    fi
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 配置 CMake
    print_info "配置 CMake..."
    cmake "$PROJECT_ROOT" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$TARGET_PREFIX" \
        -DCMAKE_CXX_FLAGS="-O2 -march=armv8-a+crc+crypto"
    
    # 编译
    print_info "开始编译（使用 $(nproc) 个核心）..."
    make -j$(nproc)
    
    # 检查可执行文件
    print_info "检查可执行文件..."
    local binaries=("src/rs485d/rs485d" "src/modbusd/modbusd" "src/webcfg/webcfg")
    for bin in "${binaries[@]}"; do
        if [ ! -f "$bin" ]; then
            print_error "编译失败: $bin 不存在"
            exit 1
        fi
        
        # 显示文件信息
        local size=$(du -h "$bin" | cut -f1)
        print_info "  ✓ $bin ($size)"
        
        # 检查架构
        local arch=$(file "$bin" | grep -o "ARM aarch64")
        if [ -z "$arch" ]; then
            print_warning "  警告: $bin 可能不是 ARM64 架构"
        fi
    done
    
    print_info "编译完成 ✓"
    echo ""
}

# ============================================================================
# 制作 IPK 包
# ============================================================================

package_ipk() {
    print_header "制作 IPK 安装包"
    
    # 创建包目录结构
    local PKG_ROOT="$PACKAGE_DIR/gw-gateway_${IPK_VERSION}_aarch64_cortex-a53"
    rm -rf "$PKG_ROOT"
    
    print_info "创建包目录结构..."
    mkdir -p "$PKG_ROOT/opt/gw/bin"
    mkdir -p "$PKG_ROOT/opt/gw/conf"
    mkdir -p "$PKG_ROOT/etc/init.d"
    mkdir -p "$PKG_ROOT/CONTROL"
    
    # 复制可执行文件
    print_info "复制可执行文件..."
    cp "$BUILD_DIR/src/rs485d/rs485d" "$PKG_ROOT/opt/gw/bin/"
    cp "$BUILD_DIR/src/modbusd/modbusd" "$PKG_ROOT/opt/gw/bin/"
    cp "$BUILD_DIR/src/webcfg/webcfg" "$PKG_ROOT/opt/gw/bin/"
    chmod +x "$PKG_ROOT/opt/gw/bin/"*
    
    # 复制配置文件
    print_info "复制配置文件..."
    if [ -f "$PROJECT_ROOT/config/config.json" ]; then
        cp "$PROJECT_ROOT/config/config.json" "$PKG_ROOT/opt/gw/conf/"
    fi
    
    # 创建 init.d 脚本
    print_info "创建 OpenWrt init 脚本..."
    create_init_scripts "$PKG_ROOT/etc/init.d"
    
    # 创建 CONTROL 文件
    print_info "创建 CONTROL 文件..."
    cat > "$PKG_ROOT/CONTROL/control" << EOF
Package: gw-gateway
Version: ${IPK_VERSION}
Architecture: aarch64_cortex-a53
Maintainer: Gateway Project <project@example.com>
Section: net
Priority: optional
Description: Industrial Gateway for RS485/Modbus/S7/OPC-UA
 工业网关系统,支持:
 - RS-485 数据采集
 - Modbus TCP 服务器
 - Web 配置界面
 - 高性能无锁数据传输
EOF

    # 创建安装后脚本
    cat > "$PKG_ROOT/CONTROL/postinst" << 'EOF'
#!/bin/sh
# 创建运行时目录
mkdir -p /opt/gw/logs /opt/gw/data

# 设置权限
chmod +x /opt/gw/bin/*

# 启用并启动服务
/etc/init.d/gw-gateway enable
/etc/init.d/gw-gateway start

echo "Gateway 安装完成"
echo "访问 Web 界面: http://$(uci get network.lan.ipaddr):8080"
exit 0
EOF
    chmod +x "$PKG_ROOT/CONTROL/postinst"
    
    # 创建卸载前脚本
    cat > "$PKG_ROOT/CONTROL/prerm" << 'EOF'
#!/bin/sh
# 停止服务
/etc/init.d/gw-gateway stop
/etc/init.d/gw-gateway disable
exit 0
EOF
    chmod +x "$PKG_ROOT/CONTROL/prerm"
    
    # 打包 IPK
    print_info "打包 IPK..."
    cd "$PACKAGE_DIR"
    
    # 创建临时工作目录
    local WORK_DIR="$PACKAGE_DIR/work"
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    
    # 创建数据压缩包（注意：必须使用 gzip 而不是其他压缩格式）
    print_info "  创建 data.tar.gz..."
    tar --numeric-owner --owner=0 --group=0 -czf data.tar.gz -C "$PKG_ROOT" opt etc
    
    # 创建控制文件压缩包
    print_info "  创建 control.tar.gz..."
    tar --numeric-owner --owner=0 --group=0 -czf control.tar.gz -C "$PKG_ROOT/CONTROL" .
    
    # 创建 debian-binary（必须包含换行符）
    print_info "  创建 debian-binary..."
    echo "2.0" > debian-binary
    
    # 创建最终的 IPK 包（注意：使用 'ar rc' 而不是 'ar rcs'，s 参数会导致 opkg 报错）
    print_info "  打包 IPK..."
    local IPK_NAME="gw-gateway_${IPK_VERSION}_aarch64_cortex-a53.ipk"
    rm -f "../$IPK_NAME"
    ar rc "../$IPK_NAME" debian-binary control.tar.gz data.tar.gz
    
    # 清理临时文件
    cd "$PACKAGE_DIR"
    rm -rf "$WORK_DIR"
    
    # 显示结果
    local ipk_size=$(du -h "$IPK_NAME" | cut -f1)
    print_info "IPK 包创建成功: $IPK_NAME ($ipk_size)"
    print_info "IPK 路径: $PACKAGE_DIR/$IPK_NAME"
    echo ""
}

# ============================================================================
# 创建 OpenWrt init 脚本
# ============================================================================

create_init_scripts() {
    local init_dir="$1"
    
    # 创建主服务脚本（使用 procd）
    cat > "$init_dir/gw-gateway" << 'INITSCRIPT'
#!/bin/sh /etc/rc.common
# Gateway 服务启动脚本（OpenWrt procd 格式）

USE_PROCD=1

START=99
STOP=10

PROG_DIR="/opt/gw/bin"
CONF_FILE="/opt/gw/conf/config.json"
LOG_DIR="/opt/gw/logs"

start_service() {
    # 创建必要的目录
    mkdir -p "$LOG_DIR"
    mkdir -p "/opt/gw/data"
    
    # 启动 rs485d (RS-485 数据采集服务)
    procd_open_instance rs485d
    procd_set_param command "$PROG_DIR/rs485d" "$CONF_FILE"
    procd_set_param respawn 3600 5 5  # 重启策略: 阈值 最小间隔 最大重启次数
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
    
    # 等待 rs485d 启动完成
    sleep 2
    
    # 启动 modbusd (Modbus TCP 服务器)
    procd_open_instance modbusd
    procd_set_param command "$PROG_DIR/modbusd" "$CONF_FILE"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
    
    # 启动 webcfg (Web 配置界面)
    procd_open_instance webcfg
    procd_set_param command "$PROG_DIR/webcfg" "$CONF_FILE"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
}

stop_service() {
    # procd 会自动处理进程停止
    killall webcfg 2>/dev/null
    killall modbusd 2>/dev/null
    killall rs485d 2>/dev/null
}

service_triggers() {
    procd_add_reload_trigger "gw-gateway"
}
INITSCRIPT
    
    chmod +x "$init_dir/gw-gateway"
}

# ============================================================================
# 部署到设备
# ============================================================================

deploy_to_device() {
    print_header "部署到 FriendlyWrt 设备"
    
    # 检查设备连接
    if ! ssh -o ConnectTimeout=5 "$DEVICE_USER@$DEVICE_HOST" "echo ''" &> /dev/null; then
        print_error "无法连接到设备 $DEVICE_USER@$DEVICE_HOST"
        print_error "请检查:"
        print_error "  1. 设备是否开机"
        print_error "  2. 网络是否连接"
        print_error "  3. SSH 密钥是否正确"
        exit 1
    fi
    
    # 查找 IPK 包
    local IPK_NAME="gw-gateway_${IPK_VERSION}_aarch64_cortex-a53.ipk"
    local IPK_PATH="$PACKAGE_DIR/$IPK_NAME"
    
    if [ ! -f "$IPK_PATH" ]; then
        print_error "IPK 包不存在: $IPK_PATH"
        print_error "请先运行: $0 package"
        exit 1
    fi
    
    # 上传 IPK 包
    print_info "上传 IPK 包到设备..."
    scp "$IPK_PATH" "$DEVICE_USER@$DEVICE_HOST:/tmp/"
    
    # 在设备上安装
    print_info "安装 IPK 包..."
    ssh "$DEVICE_USER@$DEVICE_HOST" << REMOTECMD
        # 如果已安装旧版本,先卸载
        if opkg list-installed | grep -q "gw-gateway"; then
            echo "卸载旧版本..."
            opkg remove gw-gateway
        fi
        
        # 安装新版本
        echo "安装新版本..."
        opkg install /tmp/$IPK_NAME
        
        # 清理临时文件
        rm -f /tmp/$IPK_NAME
        
        # 显示服务状态
        echo ""
        echo "服务状态:"
        /etc/init.d/gw-gateway status
REMOTECMD
    
    print_info "部署完成 ✓"
    
    # 获取设备 IP
    local device_ip=$(ssh "$DEVICE_USER@$DEVICE_HOST" "uci get network.lan.ipaddr" 2>/dev/null || echo "$DEVICE_HOST")
    
    echo ""
    print_info "访问 Web 界面: http://${device_ip}:8080"
    print_info "Modbus TCP 端口: ${device_ip}:502"
    echo ""
}

# ============================================================================
# 直接部署（不打包 IPK）- 用于快速开发测试
# ============================================================================

deploy_direct() {
    print_header "直接部署到设备（开发模式）"
    
    # 检查设备连接
    if ! ssh -o ConnectTimeout=5 "$DEVICE_USER@$DEVICE_HOST" "echo ''" &> /dev/null; then
        print_error "无法连接到设备"
        exit 1
    fi
    
    # 创建目录
    print_info "创建目录结构..."
    ssh "$DEVICE_USER@$DEVICE_HOST" "mkdir -p $TARGET_BIN_DIR $TARGET_CONF_DIR $TARGET_LOG_DIR $TARGET_DATA_DIR"
    
    # 上传可执行文件
    print_info "上传可执行文件..."
    scp "$BUILD_DIR/src/rs485d/rs485d" "$DEVICE_USER@$DEVICE_HOST:$TARGET_BIN_DIR/"
    scp "$BUILD_DIR/src/modbusd/modbusd" "$DEVICE_USER@$DEVICE_HOST:$TARGET_BIN_DIR/"
    scp "$BUILD_DIR/src/webcfg/webcfg" "$DEVICE_USER@$DEVICE_HOST:$TARGET_BIN_DIR/"
    
    # 设置权限
    ssh "$DEVICE_USER@$DEVICE_HOST" "chmod +x $TARGET_BIN_DIR/*"
    
    # 上传配置
    print_info "上传配置文件..."
    if [ -f "$PROJECT_ROOT/config/config.json" ]; then
        scp "$PROJECT_ROOT/config/config.json" "$DEVICE_USER@$DEVICE_HOST:$TARGET_CONF_DIR/"
    fi
    
    print_info "直接部署完成 ✓"
    print_info ""
    print_info "手动启动服务:"
    print_info "  ssh $DEVICE_USER@$DEVICE_HOST"
    print_info "  $TARGET_BIN_DIR/rs485d $TARGET_CONF_DIR/config.json &"
    print_info "  $TARGET_BIN_DIR/modbusd $TARGET_CONF_DIR/config.json &"
    print_info "  $TARGET_BIN_DIR/webcfg $TARGET_CONF_DIR/config.json &"
    echo ""
}

# ============================================================================
# 在设备上运行测试
# ============================================================================

test_on_device() {
    print_header "在设备上运行测试"
    
    ssh "$DEVICE_USER@$DEVICE_HOST" << 'REMOTECMD'
        echo "=== 系统信息 ==="
        uname -a
        echo ""
        
        echo "=== 进程状态 ==="
        ps | grep -E "rs485d|modbusd|webcfg" || echo "服务未运行"
        echo ""
        
        echo "=== USB 设备 ==="
        ls -l /dev/ttyUSB* 2>/dev/null || echo "未找到 USB 串口设备"
        echo ""
        
        echo "=== 共享内存 ==="
        ipcs -m
        echo ""
        
        echo "=== 日志文件 ==="
        ls -lh /opt/gw/logs/ 2>/dev/null || echo "日志目录不存在"
        echo ""
        
        echo "=== Modbus TCP 测试 ==="
        nc -zv 127.0.0.1 502 2>&1 || echo "Modbus 端口未监听"
        echo ""
        
        echo "=== Web 界面测试 ==="
        wget -q -O - http://127.0.0.1:8080/api/status 2>/dev/null || echo "Web 服务未响应"
REMOTECMD
}

# ============================================================================
# 主流程
# ============================================================================

main() {
    local command="${1:-deploy}"
    
    case "$command" in
        build)
            check_prerequisites
            build_project
            ;;
        
        package)
            check_prerequisites
            build_project
            package_ipk
            ;;
        
        deploy)
            check_prerequisites
            build_project
            package_ipk
            deploy_to_device
            ;;
        
        deploy-direct)
            check_prerequisites
            build_project
            deploy_direct
            ;;
        
        install)
            deploy_to_device
            ;;
        
        test)
            test_on_device
            ;;
        
        *)
            echo "用法: $0 {build|package|deploy|deploy-direct|install|test}"
            echo ""
            echo "命令说明:"
            echo "  build         - 仅编译项目"
            echo "  package       - 编译并制作 IPK 包"
            echo "  deploy        - 编译、打包并部署到设备（推荐）"
            echo "  deploy-direct - 直接部署可执行文件（快速开发测试）"
            echo "  install       - 仅安装已有的 IPK 包"
            echo "  test          - 在设备上运行测试"
            echo ""
            echo "示例:"
            echo "  $0 deploy        # 完整部署流程"
            echo "  $0 deploy-direct # 快速测试（不打包 IPK）"
            echo "  $0 test          # 测试设备状态"
            exit 1
            ;;
    esac
}

main "$@"
