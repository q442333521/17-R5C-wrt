#!/bin/bash
################################################################################
# FriendlyWrt 编译和部署脚本（最终修复版）
# 
# 关键修复：IPK 文件顺序必须是 debian-binary, control.tar.gz, data.tar.gz
#
################################################################################

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt"
PACKAGE_DIR="$PROJECT_ROOT/package"
IPK_VERSION="1.0.0"

DEVICE_HOST="${DEVICE_HOST:-100.121.179.13}"
DEVICE_USER="${DEVICE_USER:-root}"
DEVICE_PASS="${DEVICE_PASS:-!Wangzeyu166!@#}"

TARGET_PREFIX="/opt/gw"
TARGET_BIN_DIR="$TARGET_PREFIX/bin"
TARGET_CONF_DIR="$TARGET_PREFIX/conf"
TARGET_LOG_DIR="$TARGET_PREFIX/logs"
TARGET_DATA_DIR="$TARGET_PREFIX/data"

SSH_OPTS=(-oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -oLogLevel=ERROR)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        print_error "缺少命令: $1"
        [ -n "${2:-}" ] && print_info "安装方法: $2"
        exit 1
    fi
}

ssh_exec() {
    sshpass -p "$DEVICE_PASS" ssh "${SSH_OPTS[@]}" "$DEVICE_USER@$DEVICE_HOST" "$@"
}

scp_push() {
    sshpass -p "$DEVICE_PASS" scp "${SSH_OPTS[@]}" "$1" "$DEVICE_USER@$DEVICE_HOST:$2"
}

check_prerequisites() {
    print_header "检查前提条件"
    
    local tools=("cmake" "make" "gcc" "g++" "ar" "tar")
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            print_error "$tool 未安装"
            exit 1
        fi
    done
    print_info "编译工具检查完成 ✓"
    
    if ! command -v sshpass &> /dev/null; then
        print_warning "sshpass 未安装，将无法自动部署"
        print_info "安装方法: sudo apt install sshpass"
    else
        print_info "sshpass 已安装 ✓"
    fi
    
    if ! pkg-config --exists libmodbus; then
        print_error "libmodbus 未安装"
        print_info "安装方法: sudo apt install libmodbus-dev"
        exit 1
    fi
    
    if ! pkg-config --exists jsoncpp; then
        print_error "jsoncpp 未安装"
        print_info "安装方法: sudo apt install libjsoncpp-dev"
        exit 1
    fi
    print_info "依赖库检查完成 ✓"
    
    echo ""
}

build_project() {
    print_header "编译项目"
    
    if [ -d "$BUILD_DIR" ]; then
        print_info "清理旧的构建目录..."
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    print_info "配置 CMake..."
    cmake "$PROJECT_ROOT" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$TARGET_PREFIX" \
        -DCMAKE_CXX_FLAGS="-O2 -march=armv8-a+crc+crypto"
    
    print_info "开始编译（使用 $(nproc) 个核心）..."
    make -j$(nproc)
    
    print_info "检查编译产物..."
    local binaries=("src/rs485d/rs485d" "src/modbusd/modbusd" "src/webcfg/webcfg")
    for bin in "${binaries[@]}"; do
        if [ ! -f "$bin" ]; then
            print_error "编译失败: $bin 不存在"
            exit 1
        fi
        local size=$(du -h "$bin" | cut -f1)
        print_info "  ✓ $bin ($size)"
    done
    
    print_info "编译完成 ✓"
    echo ""
}

package_ipk() {
    print_header "制作 IPK 安装包"
    
    local PKG_ROOT="$PACKAGE_DIR/ipk-root"
    rm -rf "$PKG_ROOT"
    
    print_info "创建包目录结构..."
    mkdir -p "$PKG_ROOT/opt/gw/bin"
    mkdir -p "$PKG_ROOT/opt/gw/conf"
    mkdir -p "$PKG_ROOT/etc/init.d"
    
    print_info "复制可执行文件..."
    cp "$BUILD_DIR/src/rs485d/rs485d" "$PKG_ROOT/opt/gw/bin/"
    cp "$BUILD_DIR/src/modbusd/modbusd" "$PKG_ROOT/opt/gw/bin/"
    cp "$BUILD_DIR/src/webcfg/webcfg" "$PKG_ROOT/opt/gw/bin/"
    chmod 755 "$PKG_ROOT/opt/gw/bin/"*
    
    print_info "复制配置文件..."
    if [ -f "$PROJECT_ROOT/config/config.json" ]; then
        cp "$PROJECT_ROOT/config/config.json" "$PKG_ROOT/opt/gw/conf/"
        chmod 644 "$PKG_ROOT/opt/gw/conf/config.json"
    fi
    
    print_info "创建 init.d 脚本..."
    create_init_script "$PKG_ROOT/etc/init.d/gw-gateway"
    
    local CONTROL_DIR="$PACKAGE_DIR/ipk-control"
    rm -rf "$CONTROL_DIR"
    mkdir -p "$CONTROL_DIR"
    
    print_info "创建 control 文件..."
    cat > "$CONTROL_DIR/control" << EOF
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

    cat > "$CONTROL_DIR/postinst" << 'EOF'
#!/bin/sh
mkdir -p /opt/gw/logs /opt/gw/data
chmod 755 /opt/gw/bin/*
/etc/init.d/gw-gateway enable
/etc/init.d/gw-gateway start
echo "Gateway 安装完成"
echo "访问 Web 界面: http://\$(uci get network.lan.ipaddr 2>/dev/null || echo 192.168.2.1):8080"
exit 0
EOF
    chmod 755 "$CONTROL_DIR/postinst"
    
cat > "$CONTROL_DIR/prerm" << 'EOF'
#!/bin/sh
/etc/init.d/gw-gateway stop 2>/dev/null || true
/etc/init.d/gw-gateway disable 2>/dev/null || true
exit 0
EOF
    chmod 755 "$CONTROL_DIR/prerm"

    print_info "生成 md5sums..."
    python3 - "$PKG_ROOT" "$CONTROL_DIR/md5sums" <<'PY'
import hashlib, os, sys
root = os.path.abspath(sys.argv[1])
out_path = sys.argv[2]
entries = []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames.sort()
    filenames.sort()
    for name in filenames:
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        h = hashlib.md5()
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b''):
                if not chunk:
                    break
                h.update(chunk)
        entries.append(f"{h.hexdigest()}  {rel}\n")
with open(out_path, 'w') as out:
    out.writelines(entries)
PY
    chmod 644 "$CONTROL_DIR/md5sums"

    print_info "创建 conffiles..."
    cat > "$CONTROL_DIR/conffiles" <<'EOF'
/opt/gw/conf/config.json
EOF
    chmod 644 "$CONTROL_DIR/conffiles"
    
    print_info "打包 IPK..."
    local WORK_DIR="$PACKAGE_DIR/ipk-work"
    rm -rf "$WORK_DIR"
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    
    print_info "  创建 control.tar.gz..."
    tar --format=ustar \
        --numeric-owner --owner=0 --group=0 \
        --sort=name --mtime='@0' \
        --transform='s,^,./,' \
        -cf control.tar -C "$CONTROL_DIR" control postinst prerm md5sums conffiles
    gzip -n control.tar
    
    print_info "  创建 data.tar.gz..."
    tar --format=ustar \
        --numeric-owner --owner=0 --group=0 \
        --sort=name --mtime='@0' \
        --transform='s,^,./,' \
        -cf data.tar -C "$PKG_ROOT" opt etc
    gzip -n data.tar
    
    print_info "  创建 debian-binary..."
    echo "2.0" > debian-binary
    
    # ==================== 关键修复 ====================
    # IPK 包文件顺序必须是：debian-binary, control.tar.gz, data.tar.gz
    # ==================================================
    print_info "  使用 ar 打包（顺序：debian-binary, control.tar.gz, data.tar.gz）..."
    local IPK_NAME="gw-gateway_${IPK_VERSION}_aarch64_cortex-a53.ipk"
    rm -f "$PACKAGE_DIR/$IPK_NAME"
    ar rv "$PACKAGE_DIR/$IPK_NAME" debian-binary control.tar.gz data.tar.gz
    
    cd "$PACKAGE_DIR"
    rm -rf "$WORK_DIR" "$PKG_ROOT" "$CONTROL_DIR"
    
    local ipk_size=$(du -h "$IPK_NAME" | cut -f1)
    print_info "IPK 包创建成功: $IPK_NAME ($ipk_size)"
    print_info "IPK 路径: $PACKAGE_DIR/$IPK_NAME"
    
    print_info "验证 IPK 包格式..."
    local file_output
    file_output="$(file "$IPK_NAME")"
    if echo "$file_output" | grep -q "Debian binary package"; then
        print_info "  ✓ 格式正确 ($file_output)"
    else
        print_error "  ✗ ar 格式错误"
        echo "$file_output"
        exit 1
    fi
    
    local files=($(ar t "$IPK_NAME"))
    print_info "  文件列表: ${files[@]}"
    if [ "${files[0]}" = "debian-binary" ] && [ "${files[1]}" = "control.tar.gz" ] && [ "${files[2]}" = "data.tar.gz" ]; then
        print_info "  ✓ 文件顺序正确"
    else
        print_error "  ✗ 文件顺序错误"
        print_error "  当前顺序: ${files[@]}"
        print_error "  正确顺序: debian-binary control.tar.gz data.tar.gz"
        exit 1
    fi
    
    echo ""
}

create_init_script() {
    local output_file="$1"
    
    cat > "$output_file" << 'INITSCRIPT'
#!/bin/sh /etc/rc.common
USE_PROCD=1
START=99
STOP=10

PROG_DIR="/opt/gw/bin"
CONF_FILE="/opt/gw/conf/config.json"
LOG_DIR="/opt/gw/logs"

start_service() {
    mkdir -p "$LOG_DIR" "/opt/gw/data"
    
    procd_open_instance rs485d
    procd_set_param command "$PROG_DIR/rs485d" "$CONF_FILE"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
    
    sleep 2
    
    procd_open_instance modbusd
    procd_set_param command "$PROG_DIR/modbusd" "$CONF_FILE"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
    
    procd_open_instance webcfg
    procd_set_param command "$PROG_DIR/webcfg" "$CONF_FILE"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param user root
    procd_close_instance
}

stop_service() {
    killall webcfg 2>/dev/null || true
    killall modbusd 2>/dev/null || true
    killall rs485d 2>/dev/null || true
}

service_triggers() {
    procd_add_reload_trigger "gw-gateway"
}
INITSCRIPT
    
    chmod 755 "$output_file"
}

deploy_to_device() {
    print_header "部署到 FriendlyWrt 设备"
    
    require_cmd sshpass "sudo apt install sshpass"
    
    print_info "检查设备连接 ($DEVICE_USER@$DEVICE_HOST)..."
    if ! ssh_exec "echo 'SSH 连接成功'" &> /dev/null; then
        print_error "无法连接到设备"
        exit 1
    fi
    print_info "设备连接正常 ✓"
    
    local IPK_NAME="gw-gateway_${IPK_VERSION}_aarch64_cortex-a53.ipk"
    local IPK_PATH="$PACKAGE_DIR/$IPK_NAME"
    
    if [ ! -f "$IPK_PATH" ]; then
        print_error "IPK 包不存在: $IPK_PATH"
        exit 1
    fi
    
    print_info "上传 IPK 包到设备..."
    scp_push "$IPK_PATH" "/tmp/$IPK_NAME"
    
    print_info "安装 IPK 包..."
    ssh_exec bash <<REMOTECMD
set -e
if opkg list-installed | grep -q "^gw-gateway"; then
    echo "[远端] 卸载旧版本..."
    opkg remove gw-gateway 2>/dev/null || true
fi
echo "[远端] 安装新版本..."
opkg install /tmp/$IPK_NAME
rm -f /tmp/$IPK_NAME
echo ""
echo "[远端] 服务状态:"
/etc/init.d/gw-gateway status || true
REMOTECMD
    
    print_info "部署完成 ✓"
    
    local device_ip=$(ssh_exec "uci get network.lan.ipaddr 2>/dev/null" || echo "$DEVICE_HOST")
    
    echo ""
    print_info "访问 Web 界面: http://${device_ip}:8080"
    print_info "Modbus TCP 端口: ${device_ip}:502"
    echo ""
}

sync_direct() {
    print_header "快速同步（开发模式）"
    
    require_cmd sshpass "sudo apt install sshpass"
    
    local binaries=("$BUILD_DIR/src/rs485d/rs485d" "$BUILD_DIR/src/modbusd/modbusd" "$BUILD_DIR/src/webcfg/webcfg")
    for bin in "${binaries[@]}"; do
        if [ ! -f "$bin" ]; then
            print_error "未找到编译产物: $bin"
            exit 1
        fi
    done
    
    print_info "创建远端目录..."
    ssh_exec "mkdir -p $TARGET_BIN_DIR $TARGET_CONF_DIR $TARGET_LOG_DIR $TARGET_DATA_DIR"
    
    print_info "上传二进制文件..."
    for bin in "${binaries[@]}"; do
        scp_push "$bin" "$TARGET_BIN_DIR/"
    done
    ssh_exec "chmod 755 $TARGET_BIN_DIR/*"
    
    if [ -f "$PROJECT_ROOT/config/config.json" ]; then
        print_info "上传配置文件..."
        scp_push "$PROJECT_ROOT/config/config.json" "$TARGET_CONF_DIR/"
    fi
    
    print_info "重启服务..."
    ssh_exec "/etc/init.d/gw-gateway restart" || true
    ssh_exec "/etc/init.d/gw-gateway status" || true
    
    print_info "同步完成 ✓"
    echo ""
}

remote_status() {
    require_cmd sshpass "sudo apt install sshpass"
    ssh_exec "/etc/init.d/gw-gateway status" || echo "服务未运行或未安装"
}

remote_restart() {
    require_cmd sshpass "sudo apt install sshpass"
    print_info "重启服务..."
    ssh_exec "/etc/init.d/gw-gateway restart"
    ssh_exec "/etc/init.d/gw-gateway status" || true
}

remote_stop() {
    require_cmd sshpass "sudo apt install sshpass"
    print_info "停止服务..."
    ssh_exec "/etc/init.d/gw-gateway stop"
}

show_usage() {
    cat << EOF
用法: $0 [命令]

命令:
  build        - 仅编译项目
  package      - 编译并制作 IPK 包
  deploy       - 编译、打包并自动部署到设备（推荐）
  sync         - 快速同步二进制（开发测试用）
  status       - 查看设备上的服务状态
  restart      - 重启设备上的服务
  stop         - 停止设备上的服务
  help         - 显示此帮助信息

环境变量:
  DEVICE_HOST  - 设备 IP 地址（默认: 100.121.179.13）
  DEVICE_USER  - SSH 用户名（默认: root）
  DEVICE_PASS  - SSH 密码（默认: !Wangzeyu166!@#）

示例:
  $0 deploy              # 完整部署流程
  $0 sync                # 快速测试（不打包 IPK）
  $0 status              # 查看服务状态
  DEVICE_HOST=192.168.2.1 $0 deploy  # 指定设备 IP
EOF
}

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
        
        sync)
            build_project
            sync_direct
            ;;
        
        status)
            remote_status
            ;;
        
        restart)
            remote_restart
            ;;
        
        stop)
            remote_stop
            ;;
        
        help|--help|-h)
            show_usage
            ;;
        
        *)
            print_error "未知命令: $command"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
