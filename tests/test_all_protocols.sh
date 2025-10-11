#!/bin/bash
# =============================================================================
# 网关协议综合测试脚本
# 用于测试 Modbus TCP, S7 PLC, OPC UA 三种协议
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}网关协议综合测试${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""

# =============================================================================
# 检查依赖
# =============================================================================

check_python_package() {
    local package=$1
    python3 -c "import $package" 2>/dev/null
    return $?
}

echo -e "${YELLOW}[1/5] 检查依赖...${NC}"

if ! command -v python3 &> /dev/null; then
    echo -e "${RED}❌ 需要 Python 3${NC}"
    exit 1
fi

MISSING_PACKAGES=""

if ! check_python_package "pymodbus"; then
    MISSING_PACKAGES="$MISSING_PACKAGES pymodbus"
fi

if ! check_python_package "snap7"; then
    MISSING_PACKAGES="$MISSING_PACKAGES python-snap7"
fi

if ! check_python_package "opcua"; then
    MISSING_PACKAGES="$MISSING_PACKAGES opcua"
fi

if [ -n "$MISSING_PACKAGES" ]; then
    echo -e "${YELLOW}⚠️  缺少以下 Python 包:$MISSING_PACKAGES${NC}"
    echo ""
    echo "安装命令:"
    echo "  pip3 install$MISSING_PACKAGES"
    echo ""
    read -p "是否现在安装? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        pip3 install$MISSING_PACKAGES
    else
        echo -e "${RED}❌ 测试需要这些依赖包${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}✅ 依赖检查完成${NC}"
echo ""

# =============================================================================
# 检查网关是否运行
# =============================================================================

echo -e "${YELLOW}[2/5] 检查网关服务...${NC}"

check_process() {
    local name=$1
    local bin="$PROJECT_ROOT/build/src/$name/$name"
    if pgrep -f "$bin" > /dev/null; then
        echo -e "${GREEN}✅ $name 运行中${NC}"
        return 0
    else
        echo -e "${RED}❌ $name 未运行${NC}"
        return 1
    fi
}

ALL_RUNNING=true

if ! check_process "rs485d"; then ALL_RUNNING=false; fi
if ! check_process "modbusd"; then ALL_RUNNING=false; fi
if ! check_process "s7d"; then ALL_RUNNING=false; fi
if ! check_process "opcuad"; then ALL_RUNNING=false; fi

if [ "$ALL_RUNNING" = false ]; then
    echo ""
    echo -e "${YELLOW}部分服务未运行,是否启动?${NC}"
    read -p "[y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "启动网关服务..."
        "$PROJECT_ROOT/scripts/start.sh"
        sleep 3
    else
        echo -e "${RED}❌ 测试需要所有服务运行${NC}"
        exit 1
    fi
fi

echo ""

# =============================================================================
# 读取配置
# =============================================================================

echo -e "${YELLOW}[3/5] 读取配置...${NC}"

CONFIG_FILE="/tmp/gw-test/conf/config.json"
if [ ! -f "$CONFIG_FILE" ]; then
    CONFIG_FILE="$PROJECT_ROOT/config/config.json"
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ 配置文件未找到${NC}"
    exit 1
fi

# 使用 Python 解析 JSON
read -r ACTIVE_PROTOCOL MODBUS_PORT S7_IP S7_ENABLED OPCUA_URL OPCUA_ENABLED < <(python3 -c "
import json
with open('$CONFIG_FILE') as f:
    cfg = json.load(f)
print(
    cfg['protocol']['active'],
    cfg['protocol']['modbus']['port'],
    cfg['protocol']['s7']['plc_ip'],
    'true' if cfg['protocol']['s7']['enabled'] else 'false',
    cfg['protocol']['opcua']['server_url'],
    'true' if cfg['protocol']['opcua']['enabled'] else 'false'
)
")

echo -e "  激活协议: ${BLUE}$ACTIVE_PROTOCOL${NC}"
echo -e "  Modbus 端口: $MODBUS_PORT"
echo -e "  S7 PLC: $S7_IP (启用: $S7_ENABLED)"
echo -e "  OPC UA: $OPCUA_URL (启用: $OPCUA_ENABLED)"
echo ""

# =============================================================================
# 测试菜单
# =============================================================================

echo -e "${YELLOW}[4/5] 选择测试项目...${NC}"
echo ""
echo "1) 测试 Modbus TCP"
echo "2) 测试 S7 PLC (需要 PLC 或模拟服务器)"
echo "3) 测试 OPC UA (需要服务器或模拟服务器)"
echo "4) 启动 S7 模拟服务器"
echo "5) 启动 OPC UA 模拟服务器"
echo "6) 查看实时日志"
echo "7) 全部测试 (自动切换协议)"
echo "0) 退出"
echo ""
read -p "请选择 [1-7]: " choice

echo ""
echo -e "${YELLOW}[5/5] 执行测试...${NC}"
echo ""

# =============================================================================
# 测试函数
# =============================================================================

test_modbus() {
    echo -e "${BLUE}==== Modbus TCP 测试 ====${NC}"
    echo ""
    
    # 确保 Modbus 是激活协议
    if [ "$ACTIVE_PROTOCOL" != "modbus" ]; then
        echo -e "${YELLOW}⚠️  当前激活协议是 $ACTIVE_PROTOCOL, 切换到 modbus...${NC}"
        python3 -c "
import json
with open('$CONFIG_FILE', 'r') as f:
    cfg = json.load(f)
cfg['protocol']['active'] = 'modbus'
with open('$CONFIG_FILE', 'w') as f:
    json.dump(cfg, f, indent=2, ensure_ascii=False)
"
        echo "等待配置生效 (3秒)..."
        sleep 3
    fi
    
    echo "运行 Modbus 客户端测试..."
    python3 "$PROJECT_ROOT/tests/test_modbus_client.py"
}

test_s7() {
    echo -e "${BLUE}==== S7 PLC 测试 ====${NC}"
    echo ""
    
    # 确保 S7 是激活协议
    if [ "$ACTIVE_PROTOCOL" != "s7" ]; then
        echo -e "${YELLOW}⚠️  当前激活协议是 $ACTIVE_PROTOCOL, 切换到 s7...${NC}"
        python3 -c "
import json
with open('$CONFIG_FILE', 'r') as f:
    cfg = json.load(f)
cfg['protocol']['active'] = 's7'
cfg['protocol']['s7']['enabled'] = True
with open('$CONFIG_FILE', 'w') as f:
    json.dump(cfg, f, indent=2, ensure_ascii=False)
"
        echo "等待配置生效 (3秒)..."
        sleep 3
    fi
    
    echo "查看 S7 日志 (按 Ctrl+C 停止)..."
    tail -f /tmp/gw-test/logs/s7d.log
}

test_opcua() {
    echo -e "${BLUE}==== OPC UA 测试 ====${NC}"
    echo ""
    
    # 确保 OPC UA 是激活协议
    if [ "$ACTIVE_PROTOCOL" != "opcua" ]; then
        echo -e "${YELLOW}⚠️  当前激活协议是 $ACTIVE_PROTOCOL, 切换到 opcua...${NC}"
        python3 -c "
import json
with open('$CONFIG_FILE', 'r') as f:
    cfg = json.load(f)
cfg['protocol']['active'] = 'opcua'
cfg['protocol']['opcua']['enabled'] = True
with open('$CONFIG_FILE', 'w') as f:
    json.dump(cfg, f, indent=2, ensure_ascii=False)
"
        echo "等待配置生效 (3秒)..."
        sleep 3
    fi
    
    echo "查看 OPC UA 日志 (按 Ctrl+C 停止)..."
    tail -f /tmp/gw-test/logs/opcuad.log
}

start_s7_mock() {
    echo -e "${BLUE}==== S7 模拟服务器 ====${NC}"
    echo ""
    python3 "$PROJECT_ROOT/tests/test_s7_server.py"
}

start_opcua_mock() {
    echo -e "${BLUE}==== OPC UA 模拟服务器 ====${NC}"
    echo ""
    python3 "$PROJECT_ROOT/tests/test_opcua_server.py"
}

view_logs() {
    echo -e "${BLUE}==== 实时日志 ====${NC}"
    echo ""
    echo "按 Ctrl+C 停止..."
    echo ""
    tail -f /tmp/gw-test/logs/*.log
}

test_all() {
    echo -e "${BLUE}==== 全部测试 ====${NC}"
    echo ""
    
    echo -e "${YELLOW}1. 测试 Modbus TCP (10秒)${NC}"
    test_modbus &
    PID=$!
    sleep 10
    kill $PID 2>/dev/null || true
    
    echo ""
    echo -e "${YELLOW}2. 切换到 S7 并观察 (10秒)${NC}"
    test_s7 &
    PID=$!
    sleep 10
    kill $PID 2>/dev/null || true
    
    echo ""
    echo -e "${YELLOW}3. 切换到 OPC UA 并观察 (10秒)${NC}"
    test_opcua &
    PID=$!
    sleep 10
    kill $PID 2>/dev/null || true
    
    echo ""
    echo -e "${GREEN}✅ 全部测试完成${NC}"
}

# =============================================================================
# 执行选择的测试
# =============================================================================

case $choice in
    1)
        test_modbus
        ;;
    2)
        test_s7
        ;;
    3)
        test_opcua
        ;;
    4)
        start_s7_mock
        ;;
    5)
        start_opcua_mock
        ;;
    6)
        view_logs
        ;;
    7)
        test_all
        ;;
    0)
        echo "退出"
        exit 0
        ;;
    *)
        echo -e "${RED}❌ 无效选择${NC}"
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}✅ 测试完成${NC}"
