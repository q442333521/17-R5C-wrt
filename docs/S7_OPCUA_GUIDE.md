# S7 和 OPC UA 转发功能使用指南

## 📋 概述

网关现在支持三种协议同时运行:
- **Modbus TCP** - 标准 Modbus 从站服务器
- **S7 PLC 客户端** - 向西门子 PLC 写入数据
- **OPC UA 客户端** - 向 OPC UA 服务器写入数据

数据流:
```
RS485 测厚仪 → 共享内存环形缓冲区 → ┌─ Modbus TCP 服务器
                                    ├─ S7 PLC 客户端
                                    └─ OPC UA 客户端
```

## 🔧 安装依赖

在 Ubuntu 22.04 ARM64 上安装 S7 和 OPC UA 库:

```bash
# 赋予执行权限
chmod +x scripts/install_deps.sh

# 以 root 权限运行安装脚本
sudo ./scripts/install_deps.sh
```

安装脚本会自动:
1. 下载并编译 **Snap7 1.4.2** (S7 通信库)
2. 下载并编译 **open62541 1.3.5** (OPC UA 通信库)
3. 安装必要的开发工具和依赖

安装时间约 **5-10 分钟**,取决于网络速度和硬件性能。

## 🚀 编译项目

安装依赖后,编译项目:

```bash
# 清理旧的编译文件
rm -rf build

# 重新编译
./scripts/build.sh
```

编译成功后,会生成以下可执行文件:
- `build/src/rs485d/rs485d` - RS485 数据采集
- `build/src/modbusd/modbusd` - Modbus TCP 服务器
- `build/src/s7d/s7d` - S7 PLC 客户端 ✨ 新增
- `build/src/opcuad/opcuad` - OPC UA 客户端 ✨ 新增
- `build/src/webcfg/webcfg` - Web 配置界面

## ⚙️ 配置协议

编辑配置文件 `config/config.json`:

### 激活协议

在 `protocol` 部分设置 `active` 字段:

```json
{
  "protocol": {
    "active": "modbus",   // 可选值: "modbus", "s7", "opcua"
    ...
  }
}
```

**注意**: 同一时间只能激活一个协议,但所有守护进程都会运行。

### S7 PLC 配置

```json
{
  "protocol": {
    "s7": {
      "enabled": true,              // 是否启用 S7
      "plc_ip": "192.168.1.10",     // PLC IP 地址
      "rack": 0,                     // 机架号 (通常为 0)
      "slot": 1,                     // 槽位号 (S7-1200/1500: 1, S7-300: 2)
      "db_number": 10,               // DB 块编号
      "update_interval_ms": 50       // 更新间隔 (毫秒)
    }
  }
}
```

**数据布局 (写入 PLC DB块)**:
| 地址 | 类型 | 说明 |
|------|------|------|
| DB{db_number}.DBD0 | Float32 | 厚度值 (mm) |
| DB{db_number}.DBD4 | DWord | 时间戳低32位 |
| DB{db_number}.DBD8 | DWord | 时间戳高32位 |
| DB{db_number}.DBW12 | Word | 状态位 |
| DB{db_number}.DBW14 | Word | 序列号 |

**PLC 端配置**:
1. 在 TIA Portal 中创建数据块 (如 DB10)
2. 添加至少 16 字节的数据区域
3. 确保 PLC 允许 PUT/GET 通信

### OPC UA 配置

```json
{
  "protocol": {
    "opcua": {
      "enabled": true,                                  // 是否启用 OPC UA
      "server_url": "opc.tcp://192.168.1.20:4840",     // 服务器地址
      "security_mode": "None",                          // 安全模式: None/Sign/SignAndEncrypt
      "username": "",                                   // 用户名 (可选)
      "password": ""                                    // 密码 (可选)
    }
  }
}
```

**数据节点映射**:
| 节点 ID | 类型 | 说明 |
|---------|------|------|
| ns=2;s=Gateway.Thickness | Float | 厚度值 (mm) |
| ns=2;s=Gateway.Timestamp | Int64 | 时间戳 (Unix ms) |
| ns=2;s=Gateway.Status | UInt16 | 状态位 |
| ns=2;s=Gateway.Sequence | UInt32 | 序列号 |

**OPC UA 服务器端配置**:
1. 创建命名空间 (Namespace Index = 2)
2. 添加上述 4 个变量节点
3. 设置节点权限为 **可写**
4. 如果使用认证,创建用户账号

## 🎯 使用示例

### 示例 1: 使用 Modbus TCP (默认)

配置:
```json
{
  "protocol": {
    "active": "modbus",
    "modbus": {
      "enabled": true,
      "listen_ip": "0.0.0.0",
      "port": 1502,
      "slave_id": 1
    }
  }
}
```

启动:
```bash
./scripts/start.sh
```

测试:
```bash
python3 tests/test_modbus_client.py
```

### 示例 2: 使用 S7 PLC

配置:
```json
{
  "protocol": {
    "active": "s7",
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

启动:
```bash
./scripts/start.sh
```

查看日志:
```bash
tail -f /tmp/gw-test/logs/s7d.log
```

### 示例 3: 使用 OPC UA

配置:
```json
{
  "protocol": {
    "active": "opcua",
    "opcua": {
      "enabled": true,
      "server_url": "opc.tcp://192.168.1.20:4840",
      "security_mode": "None",
      "username": "admin",
      "password": "password123"
    }
  }
}
```

启动:
```bash
./scripts/start.sh
```

查看日志:
```bash
tail -f /tmp/gw-test/logs/opcuad.log
```

## 📊 监控和调试

### 查看实时状态

访问 Web 界面:
```
http://<设备IP>:8080
```

### 查看日志

所有日志在 `/tmp/gw-test/logs/` 目录:

```bash
# 查看所有日志
tail -f /tmp/gw-test/logs/*.log

# 查看单个服务日志
tail -f /tmp/gw-test/logs/s7d.log
tail -f /tmp/gw-test/logs/opcuad.log
```

### 统计信息

每 10 秒,S7 和 OPC UA 守护进程会输出统计信息:

```
[INFO] S7 统计: 总写入=500, 失败=0, 失败率=0.00%
[INFO] OPC UA 统计: 总写入=500, 失败=2, 失败率=0.40%
```

## 🐛 故障排查

### S7 连接失败

**问题**: 日志显示 "S7 连接失败"

**可能原因**:
1. PLC IP 地址错误
2. Rack/Slot 配置错误
3. PLC 防火墙阻止连接
4. PLC 未开启 PUT/GET 通信

**解决方法**:
```bash
# 1. 检查网络连通性
ping 192.168.1.10

# 2. 检查 S7 端口 (102)
telnet 192.168.1.10 102

# 3. 查看详细日志
tail -f /tmp/gw-test/logs/s7d.log
```

**PLC 端设置** (TIA Portal):
- 打开 "Protection & Security"
- 启用 "Permit access with PUT/GET"
- 编译并下载到 PLC

### OPC UA 连接失败

**问题**: 日志显示 "OPC UA 连接失败"

**可能原因**:
1. 服务器地址错误
2. 安全模式不匹配
3. 用户名密码错误
4. 服务器未启动

**解决方法**:
```bash
# 1. 检查服务器是否运行
telnet 192.168.1.20 4840

# 2. 使用 OPC UA 客户端工具测试连接
# (如 UAExpert, OPC UA Browser)

# 3. 查看详细日志
tail -f /tmp/gw-test/logs/opcuad.log
```

## 📝 性能优化

### S7 性能调优

```json
{
  "s7": {
    "update_interval_ms": 50    // 降低频率 (如改为 100ms)
  }
}
```

**建议值**:
- 高频采样: 50ms (20 Hz)
- 标准采样: 100ms (10 Hz)
- 低频采样: 500ms (2 Hz)

## 🔐 安全建议

### 生产环境部署

1. **使用独立 VLAN**
   - 将网关和 PLC/OPC UA 服务器放在独立网络
   - 配置防火墙规则

2. **启用 OPC UA 安全**
   ```json
   {
     "opcua": {
       "security_mode": "SignAndEncrypt",
       "username": "gateway_user",
       "password": "<strong-password>"
     }
   }
   ```

3. **使用 systemd 服务**
   ```bash
   # 安装服务
   sudo cp systemd/*.service /etc/systemd/system/
   sudo systemctl daemon-reload
   
   # 启用自启动
   sudo systemctl enable gw-s7d gw-opcuad
   
   # 启动服务
   sudo systemctl start gw-s7d gw-opcuad
   ```

## 📚 参考文档

- [Snap7 官方文档](http://snap7.sourceforge.net/)
- [open62541 官方文档](https://www.open62541.org/)
- [西门子 S7 通信手册](https://support.industry.siemens.com/)
- [OPC UA 规范](https://opcfoundation.org/developer-tools/specifications-unified-architecture)

---

**更新日期**: 2025-10-11  
**版本**: v2.0
