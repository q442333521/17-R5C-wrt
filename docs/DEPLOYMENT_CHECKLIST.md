# 📋 S7 和 OPC UA 部署检查清单

## ✅ 安装前检查

### 硬件要求
- [ ] Ubuntu 22.04 ARM64 系统
- [ ] 至少 2GB RAM
- [ ] 至少 4GB 存储空间
- [ ] 网络连接 (用于下载依赖)

### 网络要求
- [ ] 能够访问 PLC/OPC UA 服务器
- [ ] 防火墙允许以下端口:
  - [ ] S7: 端口 102 (TCP)
  - [ ] OPC UA: 端口 4840 (TCP)
  - [ ] Modbus: 端口 502/1502 (TCP)
  - [ ] Web: 端口 8080 (TCP)

## 📦 依赖安装

### 1. 基础工具
```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config wget
```
- [ ] build-essential 已安装
- [ ] cmake 已安装
- [ ] git 已安装
- [ ] pkg-config 已安装

### 2. 协议库
```bash
sudo apt install -y libmodbus-dev libjsoncpp-dev
```
- [ ] libmodbus-dev 已安装
- [ ] libjsoncpp-dev 已安装

### 3. S7 和 OPC UA 库
```bash
sudo ./scripts/install_deps.sh
```
- [ ] Snap7 1.4.2 已安装 (`/usr/local/lib/libsnap7.so`)
- [ ] open62541 1.3.5 已安装 (`/usr/local/lib/libopen62541.so`)
- [ ] 动态链接库缓存已更新 (`ldconfig`)

### 4. Python 测试工具 (可选)
```bash
pip3 install pymodbus python-snap7 opcua
```
- [ ] pymodbus 已安装
- [ ] python-snap7 已安装 (用于模拟服务器)
- [ ] opcua 已安装 (用于模拟服务器)

## 🔨 编译检查

### 1. 清理旧编译
```bash
rm -rf build
```
- [ ] 旧的 build 目录已删除

### 2. 编译项目
```bash
./scripts/build.sh
```
- [ ] CMake 配置成功
- [ ] 找到 Snap7 库
- [ ] 找到 open62541 库
- [ ] 编译无错误
- [ ] 编译无警告

### 3. 验证可执行文件
```bash
ls -lh build/src/*/
```
- [ ] `rs485d` 存在
- [ ] `modbusd` 存在
- [ ] `s7d` 存在 ✨
- [ ] `opcuad` 存在 ✨
- [ ] `webcfg` 存在

## ⚙️ 配置检查

### 1. 配置文件
```bash
cat config/config.json
```
- [ ] 配置文件存在
- [ ] JSON 格式正确

### 2. S7 配置
```json
{
  "protocol": {
    "s7": {
      "enabled": true,
      "plc_ip": "192.168.1.10",
      "rack": 0,
      "slot": 1,
      "db_number": 10,
      "update_interval_ms": 50
    }
  }
}
```
- [ ] S7 配置存在
- [ ] PLC IP 地址正确
- [ ] Rack/Slot 配置正确
- [ ] DB 块编号正确

### 3. OPC UA 配置
```json
{
  "protocol": {
    "opcua": {
      "enabled": true,
      "server_url": "opc.tcp://192.168.1.20:4840",
      "security_mode": "None",
      "username": "",
      "password": ""
    }
  }
}
```
- [ ] OPC UA 配置存在
- [ ] 服务器地址正确
- [ ] 安全模式配置正确
- [ ] 认证信息正确 (如果需要)

### 4. 激活协议
```json
{
  "protocol": {
    "active": "modbus"  // 或 "s7" 或 "opcua"
  }
}
```
- [ ] `active` 字段已设置
- [ ] 值为 "modbus", "s7", 或 "opcua" 之一

## 🌐 网络连通性检查

### 1. S7 PLC
```bash
# 检查网络连通性
ping -c 3 192.168.1.10

# 检查端口 (需要 telnet)
telnet 192.168.1.10 102
```
- [ ] PLC 可以 ping 通
- [ ] 端口 102 可以连接

### 2. OPC UA 服务器
```bash
# 检查网络连通性
ping -c 3 192.168.1.20

# 检查端口
telnet 192.168.1.20 4840
```
- [ ] 服务器可以 ping 通
- [ ] 端口 4840 可以连接

## 🚀 启动检查

### 1. 启动服务
```bash
./scripts/start.sh
```
- [ ] rs485d 启动成功
- [ ] modbusd 启动成功
- [ ] s7d 启动成功 ✨
- [ ] opcuad 启动成功 ✨
- [ ] webcfg 启动成功

### 2. 检查进程
```bash
ps aux | grep -E "rs485d|modbusd|s7d|opcuad|webcfg"
```
- [ ] 所有进程运行中
- [ ] PID 文件已创建 (`/tmp/gw-test/*.pid`)

### 3. 检查日志
```bash
ls -lh /tmp/gw-test/logs/
```
- [ ] rs485d.log 存在
- [ ] modbusd.log 存在
- [ ] s7d.log 存在 ✨
- [ ] opcuad.log 存在 ✨
- [ ] webcfg.log 存在

### 4. 查看日志内容
```bash
tail -n 20 /tmp/gw-test/logs/s7d.log
tail -n 20 /tmp/gw-test/logs/opcuad.log
```
- [ ] s7d 日志无错误
- [ ] opcuad 日志无错误
- [ ] 显示连接成功或正在重试

## 🔍 功能测试

### 1. Modbus TCP 测试
```bash
python3 tests/test_modbus_client.py
```
- [ ] 可以连接到 Modbus 服务器
- [ ] 可以读取寄存器
- [ ] 数据实时更新

### 2. S7 连接测试
```bash
# 查看日志
tail -f /tmp/gw-test/logs/s7d.log
```
预期输出:
```
[INFO] S7 连接成功: 192.168.1.10 (Rack=0, Slot=1)
[DEBUG] S7 写入成功: thickness=1.234 mm, seq=100
```
- [ ] 显示 "S7 连接成功"
- [ ] 显示 "S7 写入成功"
- [ ] 无错误信息

### 3. OPC UA 连接测试
```bash
# 查看日志
tail -f /tmp/gw-test/logs/opcuad.log
```
预期输出:
```
[INFO] OPC UA 连接成功: opc.tcp://192.168.1.20:4840
[DEBUG] OPC UA 写入成功: thickness=1.234 mm, seq=100
```
- [ ] 显示 "OPC UA 连接成功"
- [ ] 显示 "OPC UA 写入成功"
- [ ] 无错误信息

### 4. 协议切换测试
```bash
# 修改配置切换协议
vim config/config.json
# 修改 "active" 字段

# 观察日志,应该在 1 秒内生效
tail -f /tmp/gw-test/logs/s7d.log
```
- [ ] 配置修改后自动重载
- [ ] 旧协议停止写入
- [ ] 新协议开始写入

### 5. Web 界面测试
```bash
# 在浏览器访问
http://<设备IP>:8080
```
- [ ] Web 界面可以访问
- [ ] 显示实时数据
- [ ] 显示当前激活的协议
- [ ] 显示 S7/OPC UA 连接状态

## 📊 性能检查

### 1. CPU 使用率
```bash
top -p $(pgrep -d',' -f "rs485d|modbusd|s7d|opcuad|webcfg")
```
- [ ] 所有进程 CPU < 10%
- [ ] 无异常高 CPU 占用

### 2. 内存使用
```bash
ps aux | grep -E "rs485d|modbusd|s7d|opcuad|webcfg" | awk '{print $6/1024 " MB - " $11}'
```
- [ ] 每个进程 < 50 MB
- [ ] 无内存泄漏

### 3. 写入统计
```bash
# 观察 10 秒统计输出
tail -f /tmp/gw-test/logs/s7d.log | grep "统计"
```
预期输出:
```
[INFO] S7 统计: 总写入=500, 失败=0, 失败率=0.00%
```
- [ ] 失败率 < 1%
- [ ] 写入频率符合配置

## 🛡️ PLC/服务器端验证

### S7 PLC 端检查
在 TIA Portal 中:
- [ ] DB 块已创建
- [ ] DB 块大小 >= 16 字节
- [ ] 在线监控可以看到数据变化
- [ ] 厚度值实时更新
- [ ] 时间戳实时更新

### OPC UA 服务器端检查
使用 UAExpert 或其他客户端:
- [ ] 命名空间存在 (ns=2)
- [ ] 变量节点存在:
  - [ ] Gateway.Thickness
  - [ ] Gateway.Timestamp
  - [ ] Gateway.Status
  - [ ] Gateway.Sequence
- [ ] 变量值实时更新

## 🔄 稳定性测试

### 1. 断网重连测试
```bash
# 断开 PLC/服务器网络
# 观察日志

tail -f /tmp/gw-test/logs/s7d.log
```
预期行为:
- [ ] 检测到连接断开
- [ ] 每 5 秒尝试重连
- [ ] 网络恢复后自动重连
- [ ] 恢复后继续写入数据

### 2. 长时间运行测试
```bash
# 运行 24 小时
# 定期检查日志和状态
```
- [ ] 无崩溃
- [ ] 无内存泄漏
- [ ] 无异常重启
- [ ] 写入成功率 > 99%

### 3. 配置热重载测试
```bash
# 多次修改配置并保存
vim config/config.json

# 观察是否自动生效
tail -f /tmp/gw-test/logs/*.log
```
- [ ] 修改后 1 秒内生效
- [ ] 无需重启服务
- [ ] 无错误信息

## 🔐 安全检查

### 1. 文件权限
```bash
ls -l /opt/gw/conf/
ls -l /opt/gw/bin/
```
- [ ] 配置文件权限正确 (644 或更严格)
- [ ] 可执行文件权限正确 (755)

### 2. 网络安全
- [ ] 使用独立 VLAN (生产环境)
- [ ] 配置防火墙规则
- [ ] 限制访问 IP (如果可能)

### 3. OPC UA 安全
如果使用认证:
- [ ] 用户名密码不为空
- [ ] 密码强度足够
- [ ] 考虑使用加密模式 (SignAndEncrypt)

## 📝 文档检查

- [ ] 已阅读 [S7_OPCUA_GUIDE.md](docs/S7_OPCUA_GUIDE.md)
- [ ] 已阅读 [S7_OPCUA_IMPLEMENTATION.md](S7_OPCUA_IMPLEMENTATION.md)
- [ ] 已了解配置文件格式
- [ ] 已了解故障排查方法

## ✅ 生产部署清单

### Systemd 服务安装
```bash
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable gw-rs485d gw-modbusd gw-s7d gw-opcuad gw-webcfg
sudo systemctl start gw-rs485d gw-modbusd gw-s7d gw-opcuad gw-webcfg
```
- [ ] 服务文件已复制
- [ ] 服务已启用
- [ ] 服务已启动
- [ ] 开机自启动已配置

### 日志轮转
```bash
sudo vim /etc/logrotate.d/gateway
```
- [ ] 日志轮转已配置
- [ ] 日志保留策略正确

### 监控
- [ ] 配置进程监控 (如 Monit)
- [ ] 配置告警 (如 email/SMS)
- [ ] 配置性能监控 (如 Prometheus)

## 📞 支持信息

### 日志位置
- 开发环境: `/tmp/gw-test/logs/`
- 生产环境: `/var/log/gateway/`

### 配置文件
- 开发环境: `config/config.json`
- 生产环境: `/opt/gw/conf/config.json`

### 常用命令
```bash
# 查看服务状态
systemctl status gw-*

# 查看日志
journalctl -u gw-s7d -f
journalctl -u gw-opcuad -f

# 重启服务
systemctl restart gw-s7d
systemctl restart gw-opcuad

# 查看配置
cat /opt/gw/conf/config.json

# 测试连接
telnet <PLC_IP> 102
telnet <OPCUA_IP> 4840
```

---

**检查完成日期**: __________  
**检查人**: __________  
**版本**: v2.0  
**状态**: ⬜ 通过 / ⬜ 失败
