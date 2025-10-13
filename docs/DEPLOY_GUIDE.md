# FriendlyWrt R5S 开发部署指南

## 📋 环境说明

- **Windows PC**: 项目源码位置
- **Ubuntu 22.04 ARM64** (mac-pd-2204 @ 100.64.0.1): 编译环境
- **FriendlyWrt R5S** (192.168.2.1): 目标设备
- **USB 转 RS485**: CH340 芯片 (1a86:7523)

## 🚀 快速开始

### 步骤 1: 上传项目到 Ubuntu 开发机

在 Windows PowerShell 中执行：

```powershell
# 切换到项目目录
cd D:\OneDrive\p\17-R5C-wrt

# 使用 SCP 上传到 Ubuntu 开发机
scp -r . root@100.64.0.1:~/gateway-project/

# 或者使用 rsync（更快，支持增量同步）
# 需要先安装 cwRsync for Windows
rsync -avz --progress -e ssh . root@100.64.0.1:~/gateway-project/
```

**提示**: 如果项目很大，建议排除 build 目录：
```powershell
# 创建排除列表文件
@"
build/
build-*/
*.o
*.a
*.so
.git/
"@ | Out-File -Encoding UTF8 .rsyncignore

# 使用 rsync 排除
rsync -avz --progress --exclude-from=.rsyncignore -e ssh . root@100.64.0.1:~/gateway-project/
```

### 步骤 2: 在 Ubuntu 开发机上编译

SSH 登录到 Ubuntu 开发机：

```powershell
ssh root@100.64.0.1
```

在 Ubuntu 上执行：

```bash
# 进入项目目录
cd ~/gateway-project

# 安装依赖（首次需要）
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libmodbus-dev \
    libjsoncpp-dev \
    libboost-all-dev \
    pkg-config

# 给脚本添加执行权限
chmod +x build_and_deploy.sh

# 编译项目
./build_and_deploy.sh build

# 查看编译结果
ls -lh build-wrt/src/*/
```

### 步骤 3: 测试 USB 转 RS485 设备

**在 FriendlyWrt 设备上测试**（先 SSH 到 R5S）：

```bash
# 从 Windows 直接 SSH 到 FriendlyWrt
ssh root@192.168.2.1

# 或者从 Ubuntu 开发机 SSH
ssh root@192.168.2.1
```

在 FriendlyWrt 上执行：

```bash
# 1. 查看 USB 设备
lsusb
# 应该看到: Bus 007 Device 003: ID 1a86:7523 QinHeng Electronics CH340 serial converter

# 2. 查看串口设备
ls -l /dev/ttyUSB*
# 应该看到: /dev/ttyUSB0

# 3. 查看设备详细信息
dmesg | grep -i "usb\|tty" | tail -20

# 4. 测试串口通信（安装 minicom）
opkg update
opkg install minicom

# 配置并测试串口
minicom -D /dev/ttyUSB0 -b 19200

# 按 Ctrl+A Z 查看帮助
# 按 Ctrl+A X 退出
```

### 步骤 4: 部署到 FriendlyWrt

**回到 Ubuntu 开发机**，执行完整部署：

```bash
cd ~/gateway-project

# 方式 1: 完整部署（推荐，会制作 IPK 包）
./build_and_deploy.sh deploy

# 方式 2: 快速部署（不打包 IPK，用于开发测试）
./build_and_deploy.sh deploy-direct

# 方式 3: 仅打包 IPK
./build_and_deploy.sh package

# IPK 包会生成在 package/ 目录
ls -lh package/*.ipk
```

### 步骤 5: 验证部署

SSH 到 FriendlyWrt：

```bash
ssh root@192.168.2.1

# 查看服务状态
/etc/init.d/gw-gateway status

# 手动启动服务（如果未自动启动）
/etc/init.d/gw-gateway start

# 查看进程
ps | grep -E "rs485d|modbusd|webcfg"

# 查看日志
tail -f /opt/gw/logs/*.log

# 查看共享内存
ipcs -m

# 测试 Modbus TCP
nc -zv 127.0.0.1 502

# 测试 Web 界面
wget -q -O - http://127.0.0.1:8080/api/status
```

### 步骤 6: 从 Windows 访问服务

在 Windows 浏览器中访问：

```
http://192.168.2.1:8080/          # Web 配置界面
http://192.168.2.1:8080/api/status # API 状态
```

使用 Modbus 客户端测试：

```powershell
# 使用 Python 测试（需要先安装 pymodbus）
pip install pymodbus

# 创建测试脚本
@"
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.2.1', port=502)
client.connect()

# 读取寄存器 0-7
result = client.read_holding_registers(0, 8, slave=1)

if result.isError():
    print('错误:', result)
else:
    print('寄存器值:', result.registers)
    # 解析厚度值（Float32, Big-Endian）
    import struct
    thickness_bytes = struct.pack('>HH', result.registers[0], result.registers[1])
    thickness = struct.unpack('>f', thickness_bytes)[0]
    print(f'厚度值: {thickness:.3f} mm')

client.close()
"@ | Out-File -Encoding UTF8 test_modbus.py

python test_modbus.py
```

## 🔧 开发调试技巧

### 快速迭代开发

```bash
# 在 Ubuntu 开发机上

# 1. 修改代码
vim src/rs485d/main.cpp

# 2. 快速编译并部署
./build_and_deploy.sh deploy-direct

# 3. SSH 到 FriendlyWrt 查看日志
ssh root@192.168.2.1 "tail -f /opt/gw/logs/rs485d.log"
```

### 从 Windows 同步代码到 Ubuntu

```powershell
# 创建同步脚本 sync_to_ubuntu.ps1
@"
`$source = 'D:\OneDrive\p\17-R5C-wrt\'
`$target = 'root@100.64.0.1:~/gateway-project/'

# 使用 SCP 同步（简单但慢）
scp -r `$source `$target

# 或使用 rsync（需要安装 cwRsync）
# rsync -avz --progress --delete -e ssh `$source `$target

Write-Host '同步完成' -ForegroundColor Green
"@ | Out-File -Encoding UTF8 sync_to_ubuntu.ps1

# 执行同步
.\sync_to_ubuntu.ps1
```

### 远程调试日志

```powershell
# 在 Windows PowerShell 中实时查看 FriendlyWrt 日志
ssh root@192.168.2.1 "tail -f /opt/gw/logs/*.log"

# 或者使用 VS Code Remote SSH 插件
# 1. 安装 Remote SSH 扩展
# 2. 连接到 100.64.0.1
# 3. 打开 ~/gateway-project 目录
# 4. 在集成终端中执行命令
```

## 📊 性能监控

### 在 FriendlyWrt 上监控性能

```bash
ssh root@192.168.2.1

# CPU 和内存占用
top -b -n 1 | grep -E "rs485d|modbusd|webcfg"

# 实时监控
watch -n 1 'ps aux | grep -E "rs485d|modbusd|webcfg"'

# 网络连接
netstat -tulnp | grep -E "502|8080"

# 磁盘使用
df -h /opt/gw

# 日志大小
du -sh /opt/gw/logs/*
```

## ⚠️ 常见问题

### 问题 1: SSH 连接失败

```powershell
# 测试网络连通性
ping 100.64.0.1
ping 192.168.2.1

# 测试 SSH 端口
Test-NetConnection -ComputerName 100.64.0.1 -Port 22
Test-NetConnection -ComputerName 192.168.2.1 -Port 22

# 检查 SSH 密钥
ssh-add -l
```

### 问题 2: USB 设备未识别

在 FriendlyWrt 上：

```bash
# 检查 USB 驱动
lsmod | grep ch341

# 如果没有加载，手动加载
modprobe ch341

# 查看内核日志
dmesg | tail -30

# 检查 udev 规则
ls -l /etc/udev/rules.d/
```

### 问题 3: 编译失败

在 Ubuntu 上：

```bash
# 清理并重新编译
rm -rf build-wrt
./build_and_deploy.sh build

# 检查依赖
pkg-config --modversion libmodbus
pkg-config --modversion jsoncpp

# 手动安装缺失的依赖
sudo apt install -y libmodbus-dev libjsoncpp-dev
```

### 问题 4: 服务启动失败

在 FriendlyWrt 上：

```bash
# 查看详细错误
/etc/init.d/gw-gateway start

# 手动运行查看错误
/opt/gw/bin/rs485d /opt/gw/conf/config.json

# 检查配置文件
cat /opt/gw/conf/config.json

# 检查权限
ls -l /opt/gw/bin/
chmod +x /opt/gw/bin/*
```

## 🎯 下一步

1. **对接实际测厚仪**: 修改 `src/rs485d/main.cpp` 中的协议解析逻辑
2. **配置 Web 界面**: 完善 `/api/config` 接口的配置更新功能
3. **性能优化**: 根据实际运行情况调整采样频率和缓冲区大小
4. **协议扩展**: 添加 S7 或 OPC UA 客户端支持

## 📚 相关文档

- [README.md](README.md) - 项目概述
- [QUICKSTART.md](QUICKSTART.md) - Ubuntu 快速开始
- [TODO.md](TODO.md) - 开发计划
- [ARCHITECTURE.md](ARCHITECTURE.md) - 架构设计

---

**祝开发顺利！** 🚀
