# 🚀 快速启动指南（Ubuntu 22.04 arm64）

本文档将指导你在 **NanoPi R5S (Ubuntu 22.04 arm64)** 上完成项目的编译、运行和测试。

---

## 📋 前提条件

### 硬件要求
- ✅ NanoPi R5S 开发板 (2GB RAM + 32GB eMMC)
- ✅ 网络连接（用于安装依赖和远程访问）
- ✅ USB-RS485 转换器（可选，用于连接实际设备）
- ✅ 电源适配器 (5V/3A Type-C)

### 软件环境
- ✅ Ubuntu 22.04 arm64 系统（已安装）
- ✅ SSH 访问或显示器+键盘
- ✅ root 或 sudo 权限

---

## 步骤 1: 连接到 NanoPi R5S (2 分钟)

### 方式 1: SSH 连接（推荐）

```bash
# 在你的开发机上连接
ssh ubuntu@<R5S-IP地址>

# 默认密码通常是: ubuntu 或 friendlyarm
```

### 方式 2: 显示器直连

- 连接 HDMI 显示器
- 连接 USB 键盘
- 直接在终端操作

---

## 步骤 2: 上传项目文件 (3 分钟)

### 方式 1: 使用 scp 上传

```bash
# 在你的开发机上（Windows/Linux/Mac）
# 假设项目在 D:\OneDrive\p\17-R5C-wrt

# 打包项目
cd D:\OneDrive\p
tar czf 17-R5C-wrt.tar.gz 17-R5C-wrt/

# 上传到 R5S
scp 17-R5C-wrt.tar.gz ubuntu@<R5S-IP>:~

# 然后在 R5S 上解压
ssh ubuntu@<R5S-IP>
cd ~
tar xzf 17-R5C-wrt.tar.gz
cd 17-R5C-wrt
```

### 方式 2: 使用 Git 克隆

```bash
# 在 R5S 上
cd ~
git clone <repository-url>
cd 17-R5C-wrt
```

### 方式 3: 使用 U 盘

```bash
# 在 R5S 上
sudo mount /dev/sda1 /mnt
cp -r /mnt/17-R5C-wrt ~/
cd ~/17-R5C-wrt
```

---

## 步骤 3: 安装依赖 (5 分钟)

```bash
# 更新包列表
sudo apt update

# 安装编译工具
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# 安装依赖库
sudo apt install -y \
    libmodbus-dev \
    libjsoncpp-dev

# 安装 Python 测试工具（可选）
sudo apt install -y python3-pip
pip3 install pymodbus

# 验证安装
pkg-config --modversion libmodbus jsoncpp
# 应该输出类似:
# 3.1.7
# 1.9.5
```

**可能遇到的问题**:
- 如果 `apt update` 很慢，尝试更换软件源到国内镜像
- 如果某个包找不到，尝试 `apt search <包名>` 查找

---

## 步骤 4: 编译项目 (3 分钟)

```bash
# 确保在项目根目录
cd ~/17-R5C-wrt

# 赋予脚本执行权限
chmod +x scripts/*.sh

# 编译（Release 模式）
./scripts/build.sh

# 编译成功后，你将看到:
# ========================================
# Build completed successfully!
# ========================================
# 
# Executables:
# -rwxr-xr-x 1 ubuntu ubuntu 245K ... build/src/rs485d/rs485d
# -rwxr-xr-x 1 ubuntu ubuntu 198K ... build/src/modbusd/modbusd
# -rwxr-xr-x 1 ubuntu ubuntu 212K ... build/src/webcfg/webcfg
```

**编译时间**: 在 R5S 上约需 2-3 分钟

**可能遇到的问题**:
- 编译失败: 检查依赖是否完整安装
- 权限错误: 使用 `chmod +x scripts/*.sh`

---

## 步骤 5: 启动服务 (1 分钟)

```bash
# 启动所有服务（开发测试模式）
./scripts/start.sh

# 你将看到:
# =========================================
# Starting services...
#
# Starting rs485d...
# rs485d started with PID: 12345
#
# Starting modbusd...
# modbusd started with PID: 12346
#
# Starting webcfg...
# webcfg started with PID: 12347
#
# All services started!
#
# Web Interface:
#   http://localhost:8080
#   或
#   http://<R5S-IP>:8080
```

**服务说明**:
- **rs485d**: RS-485 数据采集（当前使用模拟数据）
- **modbusd**: Modbus TCP 服务器（监听端口 502）
- **webcfg**: Web 配置界面（监听端口 8080）

---

## 步骤 6: 验证运行 (5 分钟)

### 6.1 查看日志

```bash
# 方式 1: 实时查看所有日志
tail -f /tmp/gw-test/logs/*.log

# 方式 2: 分别查看
tail -f /tmp/gw-test/logs/rs485d.log
tail -f /tmp/gw-test/logs/modbusd.log
tail -f /tmp/gw-test/logs/webcfg.log

# 按 Ctrl+C 停止查看
```

**正常日志示例**:
```
==> rs485d.log <==
[2025-10-10 10:30:15] [INFO ] [rs485d] RS485 守护进程启动成功！
[2025-10-10 10:30:25] [INFO ] [rs485d] 统计: 序列号=500, 成功=500, 失败=0, 错误率=0.00%, 当前厚度=1.234 mm

==> modbusd.log <==
[2025-10-10 10:30:17] [INFO ] [modbusd] Modbus TCP server listening on 0.0.0.0:502

==> webcfg.log <==
[2025-10-10 10:30:18] [INFO ] [webcfg] HTTP server listening on port 8080
```

### 6.2 访问 Web 界面

在浏览器中打开:
```
http://<R5S-IP地址>:8080
```

例如: `http://192.168.1.100:8080`

**你将看到**:
- ✅ 系统运行状态
- ✅ 当前厚度值（模拟数据 1.0-2.0 mm）
- ✅ 数据序列号（持续递增）
- ✅ 环形缓冲区使用率
- ✅ 配置信息

**界面自动刷新**: 每 2 秒更新一次状态

### 6.3 测试 Modbus TCP

#### 方式 1: 在 R5S 本地测试

```bash
# 安装测试工具
pip3 install pymodbus

# 运行测试脚本
python3 tests/test_modbus_client.py

# 你将看到实时数据:
# ============================================================
# Modbus TCP Client Test
# ============================================================
# Target: localhost:502
# Press Ctrl+C to stop
# ============================================================
#
# [10:30:20] Thickness:   1.234 mm | Sequence:    567 | Status: 0x000F | Timestamp: 1728559820.123
# [10:30:21] Thickness:   1.456 mm | Sequence:    617 | Status: 0x000F | Timestamp: 1728559821.456
```

#### 方式 2: 从远程主机测试

```bash
# 在你的开发机上
pip3 install pymodbus

# 测试远程 R5S
python3 test_modbus_client.py <R5S-IP> 502

# 例如:
python3 test_modbus_client.py 192.168.1.100 502
```

**按 `Ctrl+C` 停止测试**

### 6.4 检查进程状态

```bash
# 查看所有服务进程
ps aux | grep -E "rs485d|modbusd|webcfg"

# 应该看到 3 个进程在运行
```

### 6.5 检查共享内存

```bash
# 查看共享内存
ipcs -m

# 应该看到名为 /gw_data_ring 的共享内存段
```

---

## 步骤 7: 停止服务

```bash
# 停止所有服务
./scripts/stop.sh

# 输出:
# =========================================
# Stopping Gateway Services
# =========================================
# Stopping webcfg (PID: 12347)...
# webcfg stopped
# Stopping modbusd (PID: 12346)...
# modbusd stopped
# Stopping rs485d (PID: 12345)...
# rs485d stopped
#
# All services stopped
```

**完全清理**（包括临时文件）:
```bash
./scripts/stop.sh clean
```

---

## 🎯 验证清单

启动后，确认以下项目：

- [ ] ✅ rs485d 进程运行正常
- [ ] ✅ modbusd 进程运行正常
- [ ] ✅ webcfg 进程运行正常
- [ ] ✅ 日志文件正常输出
- [ ] ✅ Web 界面可以访问
- [ ] ✅ Web 界面显示实时数据
- [ ] ✅ Modbus 客户端可以连接
- [ ] ✅ Modbus 客户端可以读取数据
- [ ] ✅ 数据序列号持续递增（50Hz）
- [ ] ✅ 无错误日志输出

**如果所有项目都打勾，恭喜你！系统运行正常！** 🎉

---

## ❓ 常见问题

### Q1: 编译失败，提示找不到 libmodbus

**解决方案**:
```bash
sudo apt install libmodbus-dev
pkg-config --modversion libmodbus
```

### Q2: 启动失败，提示 "Permission denied"

**解决方案**:
```bash
chmod +x scripts/*.sh
```

### Q3: Modbus 测试连接失败

**可能原因**:
1. modbusd 未启动
2. 防火墙阻止
3. 端口冲突

**解决方案**:
```bash
# 检查 modbusd 是否运行
ps aux | grep modbusd

# 检查端口是否监听
sudo netstat -tlnp | grep 502

# 检查防火墙
sudo ufw status
sudo ufw allow 502/tcp
```

### Q4: Web 界面无法访问

**可能原因**:
1. webcfg 未启动
2. 防火墙阻止 8080 端口
3. IP 地址不正确

**解决方案**:
```bash
# 检查 webcfg 是否运行
ps aux | grep webcfg

# 检查端口
sudo netstat -tlnp | grep 8080

# 允许 8080 端口
sudo ufw allow 8080/tcp

# 确认 R5S 的 IP 地址
ip addr show eth0
```

### Q5: 串口设备 /dev/ttyUSB0 打开失败

**说明**: 当前版本使用模拟数据，即使没有实际串口设备也能运行。

如果要连接真实设备:
```bash
# 检查设备是否存在
ls -l /dev/ttyUSB*

# 检查权限
sudo chmod 666 /dev/ttyUSB0

# 添加用户到 dialout 组
sudo usermod -aG dialout $USER
# 需要重新登录后生效
```

### Q6: 数据为何是模拟的？

**说明**: 当前版本为了方便测试，rs485d 生成随机厚度值 (1.0-2.0 mm)。

**实际部署步骤**:
1. 准备 USB-RS485 转换器
2. 连接到测厚仪
3. 获取测厚仪通信协议文档
4. 修改 `src/rs485d/main.cpp` 中的 `query_thickness()` 函数
5. 重新编译和测试

详见 [TODO.md](TODO.md) 中的"实际设备对接"章节。

---

## 🔍 性能验证

### CPU 和内存占用

```bash
# 查看资源占用
top -p $(pgrep -d',' -f "rs485d|modbusd|webcfg")

# 或使用 htop（更友好）
sudo apt install htop
htop
```

**正常情况**:
- rs485d: CPU ~5%, 内存 ~15MB
- modbusd: CPU ~2%, 内存 ~10MB
- webcfg: CPU ~1%, 内存 ~12MB

### 共享内存状态

```bash
# 查看共享内存使用
ipcs -m | grep gw_data_ring
```

### 数据更新频率

```bash
# 监控序列号变化（应该是 50Hz）
watch -n 0.1 "curl -s http://localhost:8080/api/status | jq '.current_data.sequence'"
```

---

## 📖 下一步

现在系统已经运行起来了，你可以：

### 1. 探索功能
- 📊 查看 Web 界面的实时数据
- 🔧 修改配置文件并重启服务
- 📈 运行性能测试

### 2. 对接实际设备
- 📝 阅读 [TODO.md](TODO.md) 中的"实际设备对接"章节
- 🔌 连接 USB-RS485 转换器和测厚仪
- ⚙️ 修改通信协议代码

### 3. 部署到生产环境
- 🚀 安装为 systemd 服务（开机自启）
- 📦 准备迁移到 FriendlyWRT

### 4. 阅读文档
- 📖 [README.md](README.md) - 项目总览
- 📝 [TODO.md](TODO.md) - 待办事项和开发计划
- 📚 [FILE_CHECKLIST.md](FILE_CHECKLIST.md) - 功能清单

---

## 🎓 安装为系统服务（开机自启）

如果你想让服务开机自启，可以安装为 systemd 服务：

```bash
# 编译并安装到系统目录
cd ~/17-R5C-wrt
./scripts/build.sh
cd build
sudo make install

# 安装 systemd 服务文件
sudo cp ../systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload

# 创建配置目录
sudo mkdir -p /opt/gw/conf
sudo cp ../config/config.json /opt/gw/conf/

# 启动服务
sudo systemctl start gw-rs485d
sudo systemctl start gw-modbusd
sudo systemctl start gw-webcfg

# 设置开机自启
sudo systemctl enable gw-rs485d
sudo systemctl enable gw-modbusd
sudo systemctl enable gw-webcfg

# 查看服务状态
sudo systemctl status gw-*
```

**查看日志**:
```bash
# systemd 日志
sudo journalctl -u gw-rs485d -f
sudo journalctl -u gw-modbusd -f
sudo journalctl -u gw-webcfg -f
```

---

## ✨ 恭喜！

你已成功在 NanoPi R5S (Ubuntu 22.04 arm64) 上运行工业网关系统！

**系统状态**: ✅ 运行正常  
**数据更新**: ✅ 50Hz  
**Web 界面**: ✅ http://<R5S-IP>:8080  
**Modbus TCP**: ✅ 端口 502  

**祝调试顺利！** 🚀

---

**问题反馈**: 如遇到问题，请查看 `/tmp/gw-test/logs/*.log` 日志文件
