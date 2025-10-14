#!/bin/sh
################################################################################
# Gateway 服务一键停止脚本
# 停止所有守护进程：rs485d, modbusd, webcfg, s7d, opcuad
################################################################################

PID_DIR="/tmp/gw-pids"
LOG_DIR="/opt/gw/logs"

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
echo "${BLUE}Gateway 服务停止脚本${NC}"
echo "${BLUE}=========================================${NC}"
echo ""

# 停止单个服务
stop_service() {
    local name=$1
    local pid_file="$PID_DIR/$name.pid"
    
    if [ ! -f "$pid_file" ]; then
        echo "${YELLOW}[提示]${NC} $name - 没有找到 PID 文件"
        # 尝试通过进程名查找并杀死
        killall "$name" 2>/dev/null && echo "${GREEN}  ✓${NC} 通过进程名停止了 $name" || true
        return 1
    fi
    
    local pid=$(cat "$pid_file")
    
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "${YELLOW}[提示]${NC} $name - 进程不存在 (PID: $pid)"
        rm -f "$pid_file"
        return 1
    fi
    
    echo "${GREEN}[停止]${NC} $name (PID: $pid)..."
    
    # 先尝试 SIGTERM (优雅退出)
    kill -TERM "$pid" 2>/dev/null
    
    # 等待最多 5 秒
    local count=0
    while kill -0 "$pid" 2>/dev/null && [ $count -lt 5 ]; do
        sleep 1
        count=$((count + 1))
    done
    
    # 如果还在运行，使用 SIGKILL
    if kill -0 "$pid" 2>/dev/null; then
        echo "${YELLOW}  →${NC} 进程未响应，强制终止..."
        kill -KILL "$pid" 2>/dev/null
        sleep 1
    fi
    
    # 最终检查
    if kill -0 "$pid" 2>/dev/null; then
        echo "${RED}  ✗${NC} $name 停止失败"
        return 1
    else
        echo "${GREEN}  ✓${NC} $name 已停止"
        rm -f "$pid_file"
        return 0
    fi
}

# 按相反的顺序停止服务（与启动顺序相反）
echo "${BLUE}开始停止服务...${NC}"
echo ""

# 1. 先停止 webcfg
stop_service "webcfg"

# 2. 停止协议守护进程
stop_service "opcuad"
stop_service "s7d"
stop_service "modbusd"

# 3. 最后停止 rs485d (数据采集)
sleep 1
stop_service "rs485d"

echo ""
echo "${BLUE}=========================================${NC}"

# 检查是否还有残留进程
echo "${BLUE}检查残留进程...${NC}"
echo ""

found_any=0
for name in rs485d modbusd webcfg s7d opcuad; do
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "${YELLOW}[警告]${NC} 发现残留进程: $name"
        echo "       使用以下命令清理: killall $name"
        found_any=1
    fi
done

if [ $found_any -eq 0 ]; then
    echo "${GREEN}✓ 没有发现残留进程${NC}"
fi

echo ""
echo "${BLUE}=========================================${NC}"
echo "${BLUE}所有服务已停止${NC}"
echo "${BLUE}=========================================${NC}"
echo ""

# 是否清理日志
if [ "$1" = "clean" ]; then
    echo "${YELLOW}清理日志文件...${NC}"
    rm -rf "$LOG_DIR"/*.log
    rm -rf "$PID_DIR"
    echo "${GREEN}✓ 日志已清理${NC}"
    echo ""
fi
