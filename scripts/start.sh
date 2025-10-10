#!/bin/bash

# Gateway Start Script
# 网关启动脚本（用于开发测试）

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
CONFIG_FILE="$PROJECT_ROOT/config/config.json"

echo "========================================="
echo "Gateway Development Start Script"
echo "========================================="

# 检查是否已编译
if [ ! -f "$BUILD_DIR/src/rs485d/rs485d" ]; then
    echo "ERROR: Project not built yet"
    echo "Please run: ./scripts/build.sh"
    exit 1
fi

# 创建临时配置目录
TEMP_CONF_DIR="/tmp/gw-test/conf"
mkdir -p "$TEMP_CONF_DIR"
cp "$CONFIG_FILE" "$TEMP_CONF_DIR/"

echo "Using config: $TEMP_CONF_DIR/config.json"
echo ""

# 启动函数
start_daemon() {
    local name=$1
    local bin=$2
    local log=$3
    
    echo "Starting $name..."
    $bin "$TEMP_CONF_DIR/config.json" > "$log" 2>&1 &
    local pid=$!
    echo "$name started with PID: $pid"
    echo $pid > "/tmp/gw-test/$name.pid"
}

# 创建日志目录
mkdir -p /tmp/gw-test/logs

# 启动各个服务
echo "Starting services..."
echo ""

start_daemon "rs485d" \
    "$BUILD_DIR/src/rs485d/rs485d" \
    "/tmp/gw-test/logs/rs485d.log"

sleep 2  # 等待共享内存创建

start_daemon "modbusd" \
    "$BUILD_DIR/src/modbusd/modbusd" \
    "/tmp/gw-test/logs/modbusd.log"

start_daemon "webcfg" \
    "$BUILD_DIR/src/webcfg/webcfg" \
    "/tmp/gw-test/logs/webcfg.log"

echo ""
echo "========================================="
echo "All services started!"
echo "========================================="
echo ""
echo "PIDs:"
cat /tmp/gw-test/*.pid
echo ""
echo "Logs:"
echo "  rs485d:  /tmp/gw-test/logs/rs485d.log"
echo "  modbusd: /tmp/gw-test/logs/modbusd.log"
echo "  webcfg:  /tmp/gw-test/logs/webcfg.log"
echo ""
echo "Web Interface:"
echo "  http://localhost:8080"
echo ""
echo "To stop all services:"
echo "  ./scripts/stop.sh"
echo ""
echo "To view logs:"
echo "  tail -f /tmp/gw-test/logs/*.log"
echo ""
