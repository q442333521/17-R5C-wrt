# 🚀 完整操作步骤

## 环境说明

- **Windows PC**: 项目源码 `D:\OneDrive\p\17-R5C-wrt\`
- **Ubuntu 22.04 ARM64**: 100.64.0.1 (编译环境)
- **FriendlyWrt R5S**: 192.168.2.1 (目标设备)
- **USB 转 RS485**: CH340 (1a86:7523)

---

## 步骤 1: 上传项目到 Ubuntu 开发机

### 方式 A: 使用 PowerShell 脚本（推荐）

在 Windows PowerShell 中：

```powershell
# 进入项目目录
cd D:\OneDrive\p\17-R5C-wrt

# 运行上传脚本
.\upload_to_ubuntu.ps1
```

### 方式 B: 手动使用 SCP

```powershell
# 进入项目目录
cd D:\OneDrive\p\17-R5C-wrt

# 上传整个项目
scp -r . root@100.64.0.1:~/gateway-project/
```

---

## 步骤 2: 在 Ubuntu 上安装依赖并编译

SSH 登录到 Ubuntu：

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

# 添加执行权限
chmod +x scripts/wrt/build_and_deploy.sh

# 编译项目
./scripts/wrt/build_and_deploy.sh build
```

**预期输出**：

```
=========================================
开始编译项目
=========================================
[INFO] 清理旧的构建目录...
[INFO] 配置 CMake...
[INFO] 开始编译（使用 4 个核心）...
[INFO] 检查可执行文件...
  ✓ src/rs485d/rs485d (234K)
  ✓ src/modbusd/modbusd (187K)
  ✓ src/webcfg/webcfg (312K)
[INFO] 编译完成 ✓
```

---

## 步骤 3: 测试 USB 转 RS485 设备

### 3.1 在 FriendlyWrt 上检查设备

SSH 到 FriendlyWrt：

```bash
ssh root@192.168.2.1
```

执行检查命令：

```bash
# 查看 USB 设备
lsusb
# 预期输出: Bus 007 Device 003: ID 1a86:7523 QinHeng Electronics

# 查看串口设备
ls -l /dev/ttyUSB*
# 预期输出: /dev/ttyUSB0

# 查看内核日志
dmesg | grep -i "usb\|tty\|ch341" | tail -20

# 测试设备权限
cat /dev/ttyUSB0
# 按 Ctrl+C 退出，如果没有权限错误说明设备正常
```

### 3.2 使用测试工具（可选）

回到 Ubuntu 开发机，编译测试工具：

```bash
cd ~/gateway-project

# 编译 USB RS485 测试工具
g++ -o test_usb_rs485 test_usb_rs485.cpp -lmodbus -std=c++17

# 上传到 FriendlyWrt
scp test_usb_rs485 root@192.168.2.1:/tmp/

# SSH 到 FriendlyWrt 并运行
ssh root@192.168.2.1
cd /tmp
./test_usb_rs485 /dev/ttyUSB0
```

---

## 步骤 4: 部署到 FriendlyWrt

回到 Ubuntu 开发机：

```bash
cd ~/gateway-project

# 完整部署（推荐，会制作 IPK 包）
./scripts/wrt/build_and_deploy.sh deploy
```

**预期输出**：

```
=========================================
制作 IPK 安装包
=========================================
[INFO] 创建包目录结构...
[INFO] 复制可执行文件...
[INFO] 复制配置文件...
[INFO] 创建 OpenWrt init 脚本...
[INFO] 创建 CONTROL 文件...
[INFO] 打包 IPK...
[INFO] IPK 包创建成功: gw-gateway_1.0.0_aarch64_cortex-a53.ipk (547K)

=========================================
部署到 FriendlyWrt 设备
=========================================
[INFO] 上传 IPK 包到设备...
[INFO] 安装 IPK 包...
安装新版本...
Installing gw-gateway (1.0.0) to root...
Configuring gw-gateway.
Gateway 安装完成
访问 Web 界面: http://192.168.2.1:8080

[INFO] 部署完成 ✓

[INFO] 访问 Web 界面: http://192.168.2.1:8080
[INFO] Modbus TCP 端口: 192.168.2.1:502
```

---

## 步骤 5: 验证部署

### 5.1 在 FriendlyWrt 上检查服务状态

```bash
ssh root@192.168.2.1

# 查看服务状态
/etc/init.d/gw-gateway status

# 查看进程
ps | grep -E "rs485d|modbusd|webcfg"

# 查看日志
tail -20 /opt/gw/logs/rs485d.log
tail -20 /opt/gw/logs/modbusd.log
tail -20 /opt/gw/logs/webcfg.log

# 查看共享内存
ipcs -m

# 测试 Modbus TCP 端口
nc -zv 127.0.0.1 502

# 测试 Web 界面
wget -q -O - http://127.0.0.1:8080/api/status
```

### 5.2 从 Windows 访问服务

在 Windows 浏览器中打开：

```
http://192.168.2.1:8080/
http://192.168.2.1:8080/api/status
```

**预期 JSON 响应**：

```json
{
  "status": "running",
  "timestamp": 1728847234567,
  "current_data": {
    "thickness": 1.523,
    "timestamp": 1728847234567,
    "sequence": 12345,
    "status": 15
  },
  "statistics": {
    "total_samples": 12345,
    "error_count": 0,
    "update_rate": 50.2
  }
}
```

### 5.3 测试 Modbus TCP

在 Windows 上使用 Python 测试：

```powershell
# 安装 pymodbus
pip install pymodbus

# 创建测试脚本
@"
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.2.1', port=502)
if client.connect():
    result = client.read_holding_registers(0, 8, slave=1)
    if not result.isError():
        print('寄存器值:', result.registers)
        
        # 解析厚度值（Float32）
        import struct
        bytes_data = struct.pack('>HH', result.registers[0], result.registers[1])
        thickness = struct.unpack('>f', bytes_data)[0]
        print(f'厚度: {thickness:.3f} mm')
    else:
        print('读取失败:', result)
    client.close()
else:
    print('连接失败')
"@ | Out-File -Encoding UTF8 test_modbus.py

python test_modbus.py
```

---

## 步骤 6: 配置实际测厚仪（重要）

当前 `rs485d` 使用的是**模拟数据**（随机生成 1.0-2.0 mm）。

要对接实际测厚仪，需要修改代码：

### 6.1 在 Ubuntu 上修改代码

```bash
cd ~/gateway-project

# 编辑 RS485 采集模块
vim src/rs485d/main.cpp
```

找到 `query_thickness()` 函数，根据实际测厚仪的 Modbus RTU 协议修改：

```cpp
// 示例：假设测厚仪使用以下 Modbus RTU 协议
// - 从站 ID: 1
// - 功能码: 03 (读保持寄存器)
// - 起始地址: 0
// - 寄存器数量: 2
// - 数据格式: Float32 Big-Endian

// 读取保持寄存器
uint16_t regs[2];
int rc = modbus_read_registers(ctx, 0, 2, regs);

if (rc == 2) {
    // 解析 Float32 (Big-Endian)
    uint32_t raw = ((uint32_t)regs[0] << 16) | regs[1];
    float thickness;
    memcpy(&thickness, &raw, sizeof(float));
    
    return thickness;
} else {
    // 读取失败
    return -1.0f;
}
```

### 6.2 重新编译并部署

```bash
# 编译
./scripts/wrt/build_and_deploy.sh build

# 快速部署（不打包 IPK）
./scripts/wrt/build_and_deploy.sh deploy-direct

# 在 FriendlyWrt 上重启服务
ssh root@192.168.2.1 "/etc/init.d/gw-gateway restart"
```

---

## 常见问题排查

### 问题 1: SSH 连接失败

```powershell
# 测试网络
ping 100.64.0.1
ping 192.168.2.1

# 测试 SSH 端口
Test-NetConnection -ComputerName 100.64.0.1 -Port 22
```

### 问题 2: 编译失败

```bash
# 清理重新编译
rm -rf build-wrt
./scripts/wrt/build_and_deploy.sh build

# 检查依赖
pkg-config --modversion libmodbus
pkg-config --modversion jsoncpp
```

### 问题 3: 服务未启动

```bash
# 手动启动查看错误
/opt/gw/bin/rs485d /opt/gw/conf/config.json

# 检查配置文件
cat /opt/gw/conf/config.json | jq .

# 检查权限
ls -l /opt/gw/bin/
chmod +x /opt/gw/bin/*
```

### 问题 4: USB 设备未识别

```bash
# 检查驱动
lsmod | grep ch341

# 手动加载驱动
modprobe ch341

# 查看内核日志
dmesg | grep -i "ch341\|usb"
```

---

## 快速命令参考

### Windows 操作

```powershell
# 上传项目
cd D:\OneDrive\p\17-R5C-wrt
.\upload_to_ubuntu.ps1

# SSH 到 Ubuntu
ssh root@100.64.0.1

# SSH 到 FriendlyWrt
ssh root@192.168.2.1
```

### Ubuntu 操作

```bash
# 编译
cd ~/gateway-project
./scripts/wrt/build_and_deploy.sh build

# 部署
./scripts/wrt/build_and_deploy.sh deploy

# 快速部署（开发测试）
./scripts/wrt/build_and_deploy.sh deploy-direct
```

### FriendlyWrt 操作

```bash
# 服务控制
/etc/init.d/gw-gateway start
/etc/init.d/gw-gateway stop
/etc/init.d/gw-gateway restart
/etc/init.d/gw-gateway status

# 查看日志
tail -f /opt/gw/logs/*.log

# 查看进程
ps | grep -E "rs485d|modbusd|webcfg"
```

---

## 下一步

1. ✅ **完成基础部署**
2. 🔄 **对接实际测厚仪**（修改 Modbus RTU 协议）
3. 🔄 **完善 Web 配置界面**
4. 🔄 **性能测试和优化**
5. ⏳ **添加 S7/OPC UA 支持**（可选）

---

**祝开发顺利！** 🎉
