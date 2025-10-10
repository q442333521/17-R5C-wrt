# 🚀 快速启动指南

本文档将指导你在 10 分钟内完成项目的编译、运行和测试。

## 前提条件

- Ubuntu 22.04 arm64 系统 (或 x86_64 用于开发测试)
- 至少 1GB 可用内存
- root 或 sudo 权限

## 步骤 1: 安装依赖 (2 分钟)

```bash
# 更新包列表
sudo apt update

# 安装编译工具
sudo apt install -y build-essential cmake git pkg-config

# 安装依赖库
sudo apt install -y libmodbus-dev libjsoncpp-dev

# 安装 Python 测试工具 (可选)
pip3 install pymodbus
```

## 步骤 2: 编译项目 (3 分钟)

```bash
# 进入项目目录
cd D:\OneDrive\p\17-R5C-wrt

# 赋予脚本执行权限
chmod +x scripts/*.sh

# 编译
./scripts/build.sh
```

编译成功后，你将看到：
```
Build completed successfully!

Executables:
-rwxr-xr-x 1 user user 245K ... build/src/rs485d/rs485d
-rwxr-xr-x 1 user user 198K ... build/src/modbusd/modbusd
-rwxr-xr-x 1 user user 212K ... build/src/webcfg/webcfg
```

## 步骤 3: 启动服务 (1 分钟)

```bash
# 启动所有服务
./scripts/start.sh
```

你将看到：
```
Starting services...

Starting rs485d...
rs485d started with PID: 12345

Starting modbusd...
modbusd started with PID: 12346

Starting webcfg...
webcfg started with PID: 12347

All services started!

Web Interface:
  http://localhost:8080
```

## 步骤 4: 验证运行 (2 分钟)

### 4.1 查看日志

```bash
# 实时查看所有日志
tail -f /tmp/gw-test/logs/*.log
```

你应该看到类似输出：
```
==> /tmp/gw-test/logs/rs485d.log <==
[2025-10-10 10:30:15] [INFO ] [rs485d] RS485 Daemon started successfully
[2025-10-10 10:30:25] [INFO ] [rs485d] Stats: seq=500, success=500, error=0, error_rate=0.00%, thickness=1.234 mm

==> /tmp/gw-test/logs/modbusd.log <==
[2025-10-10 10:30:17] [INFO ] [modbusd] Modbus TCP Daemon started successfully
[2025-10-10 10:30:17] [INFO ] [modbusd] Modbus TCP server listening on 0.0.0.0:502

==> /tmp/gw-test/logs/webcfg.log <==
[2025-10-10 10:30:18] [INFO ] [webcfg] Web Config Daemon started successfully
[2025-10-10 10:30:18] [INFO ] [webcfg] HTTP server listening on port 8080
```

### 4.2 访问 Web 界面

打开浏览器访问: **http://localhost:8080**

你将看到：
- 系统运行状态
- 当前厚度值（模拟数据 1.00-2.00 mm）
- 数据序列号
- 环形缓冲区使用率
- 配置信息

### 4.3 测试 Modbus TCP

```bash
# 运行 Modbus 客户端测试
python3 tests/test_modbus_client.py
```

你将看到实时数据更新：
```
============================================================
Modbus TCP Client Test
============================================================
Target: localhost:502
Press Ctrl+C to stop
============================================================

Connecting to localhost:502...
Connected successfully
------------------------------------------------------------
[10:30:20] Thickness:   1.234 mm | Sequence:    567 | Status: 0x000F | Timestamp: 1728559820.123
[10:30:21] Thickness:   1.456 mm | Sequence:    617 | Status: 0x000F | Timestamp: 1728559821.456
```

按 `Ctrl+C` 停止测试。

## 步骤 5: 停止服务

```bash
# 停止所有服务
./scripts/stop.sh
```

输出：
```
=========================================
Stopping Gateway Services
=========================================
Stopping webcfg (PID: 12347)...
webcfg stopped
Stopping modbusd (PID: 12346)...
modbusd stopped
Stopping rs485d (PID: 12345)...
rs485d stopped

All services stopped
```

## 常见问题

### Q1: 编译失败，提示找不到 libmodbus

**解决方案**:
```bash
sudo apt install libmodbus-dev
```

### Q2: 启动失败，提示 "Permission denied"

**解决方案**:
```bash
chmod +x scripts/*.sh
```

### Q3: Modbus 测试连接失败

**解决方案**:
```bash
# 检查 modbusd 是否运行
ps aux | grep modbusd

# 检查端口是否监听
sudo netstat -tlnp | grep 502

# 如果在远程主机上，需要修改测试脚本:
python3 tests/test_modbus_client.py <远程IP> 502
```

### Q4: Web 界面无法访问

**解决方案**:
```bash
# 检查 webcfg 是否运行
ps aux | grep webcfg

# 检查端口
sudo netstat -tlnp | grep 8080

# 查看日志
cat /tmp/gw-test/logs/webcfg.log
```

### Q5: 串口设备 /dev/ttyUSB0 打开失败

**说明**: 当前版本使用模拟数据，即使没有实际串口设备也能运行。

如果需要连接真实设备：
```bash
# 检查设备
ls -l /dev/ttyUSB*

# 修改配置文件中的设备路径
vim config/config.json
```

## 下一步

现在你已经成功运行了网关系统！接下来可以：

1. **修改配置**: 编辑 `/tmp/gw-test/conf/config.json` 并重启服务
2. **查看架构**: 阅读 `PROJECT_OVERVIEW.md` 了解系统架构
3. **连接 PLC**: 使用 Modbus TCP 客户端连接到端口 502 读取数据
4. **二次开发**: 修改源码并重新编译测试

## 性能验证

```bash
# 查看 CPU 和内存占用
top -p $(pgrep -d',' -f "rs485d|modbusd|webcfg")

# 查看共享内存状态
ipcs -m | grep gw_data_ring

# 查看数据更新频率 (应该是 50Hz)
watch -n 0.1 "curl -s http://localhost:8080/api/status | jq '.current_data.sequence'"
```

## 问题反馈

如遇到问题，请检查：
1. 日志文件: `/tmp/gw-test/logs/*.log`
2. 配置文件: `/tmp/gw-test/conf/config.json`
3. 系统日志: `dmesg | tail -50`

---

**恭喜！** 🎉 你已成功完成快速启动。现在可以开始调试和测试了！
