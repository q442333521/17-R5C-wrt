# 快速开始指南

## 🚀 5分钟快速部署

### 第一步：安装依赖

```bash
# 更新包管理器
sudo apt update

# 安装编译工具
sudo apt install -y build-essential cmake git pkg-config

# 安装依赖库
sudo apt install -y libmodbus-dev libjsoncpp-dev
```

### 第二步：安装 Snap7

```bash
cd /tmp
git clone https://github.com/SCADACS/snap7.git
cd snap7/build/unix

# ARM64编译
make -f arm_v7_linux.mk all

# 安装
sudo cp ../bin/arm_v7-linux/libsnap7.so /usr/local/lib/
sudo cp ../../src/sys/snap7.h /usr/local/include/
sudo ldconfig
```

### 第三步：编译网关

```bash
cd /path/to/17-R5C-wrt
./scripts/build_gateway.sh
```

### 第四步：配置网关

编辑配置文件：
```bash
nano config/gateway_config.json
```

关键配置项：
- `modbus_rtu.device`: 串口设备 (如 `/dev/ttyUSB0`)
- `modbus_rtu.baudrate`: 波特率 (如 `9600`)
- `gateway.mode`: 选择 `"modbus_tcp"` 或 `"s7"`
- `mapping_rules`: 配置映射规则

### 第五步：运行网关

```bash
# 前台运行（测试）
sudo ./build-gateway/gateway-bridge --config config/gateway_config.json

# 或安装为系统服务
sudo ./scripts/install_service.sh
sudo systemctl start gateway-bridge
sudo systemctl status gateway-bridge
```

### 第六步：测试连接

```bash
# 测试Modbus TCP
python3 scripts/test_gateway.py

# 或使用 mbpoll
mbpoll -m tcp -a 1 -r 100 -c 2 -t 3 127.0.0.1
```

---

## 📝 配置示例

### 示例1: Modbus RTU → Modbus TCP

```json
{
  "gateway": {
    "mode": "modbus_tcp"
  },
  "mapping_rules": [
    {
      "rule_id": "thickness-to-tcp",
      "enabled": true,
      "source": {
        "slave_id": 1,
        "function_code": 3,
        "start_address": 0,
        "register_count": 2,
        "data_type": "float",
        "poll_interval_ms": 100
      },
      "destination": {
        "protocol": "modbus_tcp",
        "start_address": 100,
        "data_type": "float"
      },
      "transform": {
        "operation": "scale",
        "scale": 1.0,
        "offset": 0.0
      }
    }
  ]
}
```

### 示例2: Modbus RTU → S7 PLC

```json
{
  "gateway": {
    "mode": "s7"
  },
  "s7": {
    "enabled": true,
    "plc_ip": "192.168.1.10",
    "rack": 0,
    "slot": 1
  },
  "mapping_rules": [
    {
      "rule_id": "thickness-to-s7",
      "enabled": true,
      "source": {
        "slave_id": 1,
        "function_code": 3,
        "start_address": 0,
        "register_count": 2,
        "data_type": "float",
        "poll_interval_ms": 100
      },
      "destination": {
        "protocol": "s7",
        "db_number": 1,
        "start_byte": 0,
        "data_type": "float"
      },
      "transform": {
        "operation": "expression",
        "expression": "x * 0.001"
      }
    }
  ]
}
```

---

## 🔧 故障排查

### 问题1: 串口设备打开失败

```bash
# 检查设备
ls -l /dev/ttyUSB*

# 添加权限
sudo chmod 666 /dev/ttyUSB0

# 或添加用户到dialout组
sudo usermod -aG dialout $USER
```

### 问题2: Modbus TCP 502端口被占用

```bash
# 检查端口占用
sudo netstat -tlnp | grep 502

# 修改配置文件中的端口号
# modbus_tcp.port: 5020
```

### 问题3: S7连接失败

- 检查PLC IP地址是否正确
- 确认Rack和Slot参数 (S7-1200通常是0, 1)
- 确保PLC允许PUT/GET通信
- 使用未优化的DB块

---

## 📊 性能测试

```bash
# Modbus 吞吐量测试
for i in {1..1000}; do
  mbpoll -m tcp -a 1 -r 100 -c 2 -t 3 127.0.0.1 > /dev/null
done

# 查看网关统计
sudo journalctl -u gateway-bridge -f
```

---

## 📚 更多文档

- [完整技术方案](GATEWAY_BRIDGE_DESIGN.md)
- [实现指南](IMPLEMENTATION_GUIDE.md)
- [用户手册](GATEWAY_README.md)

---

**立即开始**: `./scripts/build_gateway.sh` 🚀
