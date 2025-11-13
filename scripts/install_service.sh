#!/bin/bash
# Gateway Bridge 服务安装脚本

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-gateway"

echo "═══════════════════════════════════════════"
echo "  Gateway Bridge 服务安装"
echo "═══════════════════════════════════════════"
echo ""

# 检查是否以root运行
if [ "$EUID" -ne 0 ]; then
    echo "错误: 请使用 sudo 运行此脚本"
    exit 1
fi

# 检查可执行文件
if [ ! -f "${BUILD_DIR}/gateway-bridge" ]; then
    echo "错误: 找不到可执行文件"
    echo "请先运行: ./scripts/build_gateway.sh"
    exit 1
fi

# 创建目录
echo "创建目录..."
mkdir -p /etc/gateway-bridge
mkdir -p /opt/gateway-bridge
mkdir -p /var/log/gateway-bridge

# 安装可执行文件
echo "安装可执行文件..."
cp "${BUILD_DIR}/gateway-bridge" /usr/local/bin/
chmod +x /usr/local/bin/gateway-bridge

# 安装配置文件
echo "安装配置文件..."
if [ ! -f "/etc/gateway-bridge/gateway_config.json" ]; then
    cp "${PROJECT_ROOT}/config/gateway_config.json" /etc/gateway-bridge/
    echo "  ✓ 配置文件已安装"
else
    echo "  ! 配置文件已存在，跳过"
fi

# 安装systemd服务
echo "安装systemd服务..."
cp "${PROJECT_ROOT}/systemd/gateway-bridge.service" /etc/systemd/system/
systemctl daemon-reload

echo ""
echo "═══════════════════════════════════════════"
echo "  ✓ 安装完成！"
echo "═══════════════════════════════════════════"
echo ""
echo "配置文件: /etc/gateway-bridge/gateway_config.json"
echo "日志目录: /var/log/gateway-bridge/"
echo ""
echo "下一步:"
echo "  1. 编辑配置: sudo nano /etc/gateway-bridge/gateway_config.json"
echo "  2. 启动服务: sudo systemctl start gateway-bridge"
echo "  3. 查看状态: sudo systemctl status gateway-bridge"
echo "  4. 开机自启: sudo systemctl enable gateway-bridge"
echo "  5. 查看日志: sudo journalctl -u gateway-bridge -f"
echo ""
