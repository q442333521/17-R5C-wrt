#!/bin/sh
################################################################################
# Gateway 服务一键启动脚本
# 启动所有守护进程：rs485d, modbusd, webcfg, s7d, opcuad
################################################################################

BIN_DIR="/opt/gw/bin"
CONF_FILE="/opt/gw/conf/config.json"
LOG_DIR="/opt/gw/logs"
PID_DIR="/tmp/gw-pids"

# 颜色定义
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

echo "${BLUE}=========================================${NC}"
echo "${BLUE}Gateway 服务启动脚本${NC}"
echo "${BLUE}=========================================${NC}"
echo ""

# 创建必要的目录
mkdir -p "$LOG_DIR"
mkdir -p "$PID_DIR"

# 检查配置文件
if [ ! -f "$CONF_FILE" ]; then
    echo "${RED}[错误]${NC} 配置文件不存在: $CONF_FILE"
    exit 1
fi




# 启动单个服务
start_service() {
    local name=$1
    local binary="$BIN_DIR/$name"
    
    if [ ! -x "$binary" ]; then
        echo "${YELLOW}[跳过]${NC} $name - 二进制文件不存在或无执行权限"
        return 1
    fi
    

    
    # 检查是否已经在运行
    if [ -f "$PID_DIR/$name.pid" ]; then
        local old_pid=$(cat "$PID_DIR/$name.pid")
        if kill -0 "$old_pid" 2>/dev/null; then
            echo "${YELLOW}[提示]${NC} $name 已经在运行 (PID: $old_pid)"
            return 0
        fi
    fi
    
    # 启动服务
    echo "${GREEN}[启动]${NC} $name..."
    "$binary" "$CONF_FILE" > "$LOG_DIR/$name.log" 2>&1 &
    local pid=$!
    echo $pid > "$PID_DIR/$name.pid"
    
    # 等待服务启动
    sleep 1
    
    # 检查进程是否还在运行
    if kill -0 "$pid" 2>/dev/null; then
        echo "${GREEN}  ✓${NC} $name 启动成功 (PID: $pid)"
        echo "     日志: $LOG_DIR/$name.log"
        return 0
    else
        echo "${RED}  ✗${NC} $name 启动失败"
        echo "     查看日志: tail $LOG_DIR/$name.log"
        rm -f "$PID_DIR/$name.pid"
        return 1
    fi
}

# 依次启动所有服务
echo "${BLUE}开始启动服务...${NC}"
echo ""

# 记录成功启动的服务
SUCCESS_COUNT=0

# 1. 启动 rs485d (数据采集，必须最先启动)
if start_service "rs485d"; then
    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    sleep 2
fi

# 2. 启动协议守护进程
if start_service "modbusd"; then
    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
fi

sleep 1

# if start_service "s7d"; then
#     SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
# fi

# if start_service "opcuad"; then
#     SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
# fi

# 3. 最后启动 webcfg
sleep 1
if start_service "webcfg"; then
    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
fi

echo ""
echo "${BLUE}=========================================${NC}"
echo "${BLUE}服务启动完成${NC}"
echo "${BLUE}=========================================${NC}"
echo ""

# 显示运行状态
echo "${GREEN}运行中的服务 ($SUCCESS_COUNT):${NC}"
for name in rs485d modbusd webcfg s7d opcuad; do
    if [ -f "$PID_DIR/$name.pid" ]; then
        pid=$(cat "$PID_DIR/$name.pid")
        if kill -0 "$pid" 2>/dev/null; then
            echo "  ${GREEN}✓${NC} $name (PID: $pid)"
        fi
    fi
done

echo ""
if [ $SUCCESS_COUNT -eq 0 ]; then
    echo "${RED}错误: 没有服务成功启动！${NC}"
    exit 1
elif [ $SUCCESS_COUNT -lt 3 ]; then
    echo "${YELLOW}警告: 部分服务未启动，但核心服务可能正常运行${NC}"
fi

echo ""
echo "${BLUE}访问地址:${NC}"
LAN_IP=$(uci get network.lan.ipaddr 2>/dev/null || echo "localhost")
echo "  Web 界面:  http://$LAN_IP:8080"
echo "  Modbus TCP: $LAN_IP:1502"
echo ""
echo "${YELLOW}提示:${NC}"
echo "  查看日志: tail -f $LOG_DIR/*.log"
echo "  停止服务: $0/../stop.sh"
echo ""
