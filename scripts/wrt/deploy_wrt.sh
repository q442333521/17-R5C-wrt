#!/bin/bash
#
# FriendlyWrt 部署脚本
# - 使用 sshpass 将编译产物推送到目标设备
# - 安装/更新启动脚本并立即重启服务
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt"
INIT_SCRIPT="$PROJECT_ROOT/openwrt/init.d/gw-gateway"
DEFAULT_CONFIG="$PROJECT_ROOT/config/config.json"
PACKAGE_DIR="$PROJECT_ROOT/package"
BUILD_SCRIPT="$SCRIPT_DIR/build_and_deploy.sh"

DEVICE_HOST="${DEVICE_HOST:-100.121.179.13}"
DEVICE_USER="${DEVICE_USER:-root}"
DEVICE_PASS="${DEVICE_PASS:-!Wangzeyu166!@#}"
TARGET_PREFIX="${TARGET_PREFIX:-/opt/gw}"
TARGET_BIN_DIR="$TARGET_PREFIX/bin"
TARGET_CONF_DIR="$TARGET_PREFIX/conf"
TARGET_LOG_DIR="$TARGET_PREFIX/logs"
TARGET_DATA_DIR="$TARGET_PREFIX/data"

SSH_OPTS=(-oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -oLogLevel=ERROR)
BINARIES=(rs485d modbusd webcfg opcuad s7d)

usage() {
    cat <<'EOF'
用法: ./scripts/wrt/deploy_wrt.sh [命令]

命令:
  deploy        构建 IPK 包并推送到 FriendlyWrt，自动安装/启用服务
  install       使用现有 IPK 包直接推送并安装
  sync          直接同步当前 build-wrt 二进制（开发模式）
  restart       仅重启远端服务
  stop          停止远端服务
  status        查看远端服务状态
  help          显示本帮助

可通过环境变量覆盖默认配置:
  DEVICE_HOST, DEVICE_USER, DEVICE_PASS, TARGET_PREFIX
EOF
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "缺少命令: $1" >&2
        exit 1
    fi
}

ssh_exec() {
    sshpass -p "$DEVICE_PASS" ssh "${SSH_OPTS[@]}" "$DEVICE_USER@$DEVICE_HOST" "$@"
}

scp_push() {
    local src="$1"
    local dest="$2"
    sshpass -p "$DEVICE_PASS" scp "${SSH_OPTS[@]}" "$src" "$DEVICE_USER@$DEVICE_HOST:$dest"
}

ensure_build_script() {
    if [ ! -x "$BUILD_SCRIPT" ]; then
        if [ -f "$BUILD_SCRIPT" ]; then
            chmod +x "$BUILD_SCRIPT"
        else
            echo "找不到打包脚本: $BUILD_SCRIPT" >&2
            exit 1
        fi
    fi
}

build_ipk() {
    ensure_build_script
    echo "[INFO] 构建 IPK 包..."
    "$BUILD_SCRIPT" package
}

find_latest_ipk() {
    if [ ! -d "$PACKAGE_DIR" ]; then
        return 1
    fi
    local ipk_path
    ipk_path="$(python3 - "$PACKAGE_DIR" <<'PY'
import sys, pathlib
pkg_dir = pathlib.Path(sys.argv[1])
ipks = sorted(pkg_dir.glob("*.ipk"), key=lambda p: p.stat().st_mtime, reverse=True)
if ipks:
    print(ipks[0])
PY
)"
    if [ -z "$ipk_path" ]; then
        return 1
    fi
    printf '%s\n' "$ipk_path"
}

install_ipk() {
    local ipk_path="${1:-}"
    if [ -z "$ipk_path" ]; then
        ipk_path="$(find_latest_ipk)" || {
            echo "未找到 IPK 包，请先运行: $0 deploy 或 $0 install <ipk_path>" >&2
            exit 1
        }
    fi
    if [ ! -f "$ipk_path" ]; then
        if [ -f "$PACKAGE_DIR/$ipk_path" ]; then
            ipk_path="$PACKAGE_DIR/$ipk_path"
        fi
    fi
    if [ ! -f "$ipk_path" ]; then
        echo "指定的 IPK 不存在: $ipk_path" >&2
        exit 1
    fi

    local ipk_name
    ipk_name="$(basename "$ipk_path")"

    echo "[INFO] 上传 IPK 包: $ipk_name"
    scp_push "$ipk_path" "/tmp/$ipk_name"

    echo "[INFO] 安装 IPK 包..."
    sshpass -p "$DEVICE_PASS" ssh "${SSH_OPTS[@]}" "$DEVICE_USER@$DEVICE_HOST" "bash -s" <<EOF
set -e
if opkg list-installed | grep -q "^gw-gateway"; then
    echo "[远端] 卸载旧版本..."
    opkg remove gw-gateway >/dev/null 2>&1 || true
fi
echo "[远端] 安装新版本..."
opkg install /tmp/$ipk_name
rm -f /tmp/$ipk_name
/etc/init.d/gw-gateway enable
/etc/init.d/gw-gateway restart
/etc/init.d/gw-gateway status || true
EOF
    echo "[INFO] IPK 部署完成"
}

check_build_artifacts() {
    for bin in "${BINARIES[@]}"; do
        local path="$BUILD_DIR/src/$bin/$bin"
        if [ ! -f "$path" ]; then
            echo "未找到二进制: $path" >&2
            echo "请先运行 ./scripts/wrt/build_and_deploy.sh build" >&2
            exit 1
        fi
    done
}

push_files() {
    echo "[INFO] 创建远端目录..."
    ssh_exec "mkdir -p $TARGET_BIN_DIR $TARGET_CONF_DIR $TARGET_LOG_DIR $TARGET_DATA_DIR"

    echo "[INFO] 上传二进制..."
    for bin in "${BINARIES[@]}"; do
        local path="$BUILD_DIR/src/$bin/$bin"
        if [ -f "$path" ]; then
            scp_push "$path" "$TARGET_BIN_DIR/"
        fi
    done
    ssh_exec "chmod +x $TARGET_BIN_DIR/*"

    if [ -f "$DEFAULT_CONFIG" ]; then
        echo "[INFO] 上传配置文件..."
        scp_push "$DEFAULT_CONFIG" "$TARGET_CONF_DIR/config.json"
    else
        echo "[WARN] 未找到默认配置 $DEFAULT_CONFIG，跳过上传"
    fi

    echo "[INFO] 上传 init 脚本..."
    scp_push "$INIT_SCRIPT" "/etc/init.d/gw-gateway"
    ssh_exec "chmod +x /etc/init.d/gw-gateway"
}

remote_restart() {
    ssh_exec "/etc/init.d/gw-gateway stop 2>/dev/null || true"
    ssh_exec "/etc/init.d/gw-gateway enable"
    ssh_exec "/etc/init.d/gw-gateway start"
}

remote_stop() {
    ssh_exec "/etc/init.d/gw-gateway stop"
}

remote_status() {
    ssh_exec "/etc/init.d/gw-gateway status || echo '服务可能未运行'"
}

cmd="${1:-deploy}"

case "$cmd" in
    deploy)
        require_cmd sshpass
        require_cmd scp
        require_cmd ssh
        build_ipk
        install_ipk
        remote_status
        ;;
    install)
        require_cmd sshpass
        require_cmd scp
        require_cmd ssh
        install_ipk "${2:-}"
        remote_status
        ;;
    sync)
        require_cmd sshpass
        require_cmd scp
        require_cmd ssh
        check_build_artifacts
        push_files
        remote_restart
        remote_status
        ;;
    restart)
        require_cmd sshpass
        require_cmd ssh
        remote_restart
        remote_status
        ;;
    stop)
        require_cmd sshpass
        require_cmd ssh
        remote_stop
        ;;
    status)
        require_cmd sshpass
        require_cmd ssh
        remote_status
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
