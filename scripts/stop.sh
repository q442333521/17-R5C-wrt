#!/bin/bash

# Gateway Stop Script
# 网关停止脚本

echo "========================================="
echo "Stopping Gateway Services"
echo "========================================="

stop_daemon() {
    local name=$1
    local pidfile="/tmp/gw-test/$name.pid"
    
    if [ -f "$pidfile" ]; then
        local pid=$(cat "$pidfile")
        echo "Stopping $name (PID: $pid)..."
        
        if kill -0 $pid 2>/dev/null; then
            kill $pid
            
            # 等待进程退出
            for i in {1..10}; do
                if ! kill -0 $pid 2>/dev/null; then
                    echo "$name stopped"
                    break
                fi
                sleep 1
            done
            
            # 如果还在运行，强制杀死
            if kill -0 $pid 2>/dev/null; then
                echo "$name not responding, force killing..."
                kill -9 $pid
            fi
        else
            echo "$name is not running"
        fi
        
        rm -f "$pidfile"
    else
        echo "$name PID file not found"
    fi
}

# 按相反顺序停止服务
stop_daemon "webcfg"
stop_daemon "opcuad"
stop_daemon "s7d"
stop_daemon "modbusd"
stop_daemon "rs485d"

# 额外清理仍在运行的守护进程（可能缺少 PID 文件）
EXTRA_PROCS=(rs485d modbusd s7d opcuad webcfg)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

for name in "${EXTRA_PROCS[@]}"; do
    pattern="$PROJECT_ROOT/build/src/$name/$name"
    pids=$(pgrep -f "$pattern")
    if [ -n "$pids" ]; then
        echo "Force stopping lingering $name processes: $pids"
        pkill -TERM -f "$pattern" || true
        sleep 1
        pkill -KILL -f "$pattern" >/dev/null 2>&1 || true
        # 清理残留 PID 文件
        rm -f "/tmp/gw-test/$name.pid"
    fi
done

echo ""
echo "All services stopped"
echo ""

# 清理共享内存
echo "Cleaning up shared memory..."
rm -f /dev/shm/gw_data_ring

# 清理临时文件（可选）
if [ "$1" == "clean" ]; then
    echo "Cleaning up temporary files..."
    rm -rf /tmp/gw-test
fi

echo "Done"
