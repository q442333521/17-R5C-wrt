#!/bin/bash
#
# 本地 WRT 构建一键启动脚本
# - 启动 build-wrt 目录内的各个守护进程
# - 启动 Modbus 客户端测试脚本
#

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-wrt"
CONFIG_FILE="$PROJECT_ROOT/config/config.json"
TMP_ROOT="/tmp/gw-wrt-test"
PID_DIR="$TMP_ROOT/pids"
LOG_DIR="$TMP_ROOT/logs"

declare -a CHILD_PIDS=()

terminate_pid() {
    local pid=$1
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup_pid_files() {
    if [ ! -d "$PID_DIR" ]; then
        return
    fi

    for pid_file in "$PID_DIR"/*.pid; do
        [ -f "$pid_file" ] || continue
        local pid
        pid="$(cat "$pid_file" 2>/dev/null || true)"
        if [ -n "${pid:-}" ]; then
            echo "[CLEANUP] 终止残留进程 $(basename "$pid_file" .pid) (PID=$pid)"
            terminate_pid "$pid"
        fi
        rm -f "$pid_file"
    done
}

cleanup_residual_processes() {
    local binaries=("rs485d" "modbusd" "s7d" "opcuad" "webcfg")
    for name in "${binaries[@]}"; do
        local binary="$BUILD_DIR/src/$name/$name"
        [ -x "$binary" ] || continue
        local pids=""
        pids+=" $(pgrep -f "$binary" || true)"
        pids+=" $(pgrep -f "build-wrt/src/$name/$name" || true)"
        pids+=" $(pgrep -x "$name" || true)"
        if [ -n "$pids" ]; then
            for pid in $pids; do
                if kill -0 "$pid" 2>/dev/null; then
                    echo "[CLEANUP] 杀死残留 $name 进程 (PID=$pid)"
                    terminate_pid "$pid"
                fi
            done
        fi
    done
}

cleanup() {
    echo ""
    echo "[CLEANUP] 停止所有本地测试进程..."
    for pid in "${CHILD_PIDS[@]}"; do
        terminate_pid "$pid"
    done
    cleanup_pid_files
    cleanup_residual_processes
    rm -rf "$PID_DIR" 2>/dev/null || true
    echo "[CLEANUP] 完成"
}
trap cleanup EXIT INT TERM

check_binary() {
    local name=$1
    local path="$BUILD_DIR/src/$name/$name"
    if [ ! -x "$path" ]; then
        echo "[WARN] 未找到可执行文件: $path (跳过启动 $name)" >&2
        return 1
    fi
    echo "$path"
}

start_daemon() {
    local name=$1
    local bin=$2
    echo "[START] 启动 $name ..."
    mkdir -p "$PID_DIR" "$LOG_DIR"
    "$bin" "$CONFIG_FILE" > "$LOG_DIR/${name}.log" 2>&1 &
    local pid=$!
    CHILD_PIDS+=("$pid")
    echo "$pid" > "$PID_DIR/${name}.pid"
    echo "[START] $name PID=$pid 日志: $LOG_DIR/${name}.log"
}

echo "========================================="
echo "本地 WRT 构建测试启动脚本"
echo "========================================="
echo "[INFO] 项目根目录: $PROJECT_ROOT"
echo "[INFO] 构建目录:   $BUILD_DIR"
echo "[INFO] 配置文件:   $CONFIG_FILE"
echo ""

echo "[INFO] 清理历史运行残留..."
cleanup_pid_files
cleanup_residual_processes
rm -rf "$PID_DIR" 2>/dev/null || true

if [ ! -f "$CONFIG_FILE" ]; then
    echo "[ERROR] 未找到配置文件: $CONFIG_FILE"
    exit 1
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "[ERROR] 未找到 build-wrt 目录, 请先运行:"
    echo "  ./scripts/wrt/build_and_deploy.sh build"
    exit 1
fi

mkdir -p "$LOG_DIR"

RS485_BIN=$(check_binary "rs485d") || true
MODBUS_BIN=$(check_binary "modbusd") || true
WEBCFG_BIN=$(check_binary "webcfg") || true
OPCUA_BIN=$(check_binary "opcuad") || true
S7_BIN=$(check_binary "s7d") || true

if [ -n "${RS485_BIN:-}" ]; then
    start_daemon "rs485d" "$RS485_BIN"
    sleep 2
fi

if [ -n "${MODBUS_BIN:-}" ]; then
    start_daemon "modbusd" "$MODBUS_BIN"
fi

if [ -n "${S7_BIN:-}" ]; then
    start_daemon "s7d" "$S7_BIN"
fi

if [ -n "${OPCUA_BIN:-}" ]; then
    start_daemon "opcuad" "$OPCUA_BIN"
fi

if [ -n "${WEBCFG_BIN:-}" ]; then
    start_daemon "webcfg" "$WEBCFG_BIN"
fi

echo ""
echo "[INFO] 所有守护进程已启动, 开始 Modbus 客户端测试 (Ctrl+C 结束测试并自动停止守护进程)"
mkdir -p "$LOG_DIR"

MODBUS_TEST_PORT=$(python3 - <<'PY' "$CONFIG_FILE"
import json, sys
from pathlib import Path
cfg_path = Path(sys.argv[1])
try:
    data = json.loads(cfg_path.read_text())
    port = data.get("protocol", {}).get("modbus", {}).get("port", 502)
except Exception:
    port = 502
print(port)
PY
)

echo "[INFO] Modbus 客户端测试端口: $MODBUS_TEST_PORT"
sleep 2

stdbuf -oL python3 "$PROJECT_ROOT/tests/test_modbus_client.py" localhost "$MODBUS_TEST_PORT" \
    2>&1 | tee "$LOG_DIR/modbus_client.log"

echo ""
echo "[INFO] Modbus 客户端测试已结束"
