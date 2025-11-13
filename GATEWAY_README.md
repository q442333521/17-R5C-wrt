# 工业协议网关 - 超越商用方案

## 🎯 项目简介

这是一个**开源的工业协议转换网关**，运行在 NanoPi R5S (FriendlyWrt/Linux)上，实现：

- ✅ **Modbus RTU ↔ Modbus TCP** 双向通讯
- ✅ **Modbus RTU ↔ S7 PLC** 双向通讯
- ✅ **灵活的地址映射** 和数据类型转换
- ✅ **数学运算支持** (缩放、表达式计算)
- ✅ **Web配置界面** 和实时监控
- ✅ **7×24小时稳定运行**

### 💡 为什么选择这个方案？

**vs 商用网关 (如460MMSC, $600)**

| 特性 | 460MMSC | 本方案 | 优势 |
|------|---------|--------|------|
| 协议支持 | RTU↔TCP | RTU↔TCP, RTU↔S7 | ✅ 更多 |
| 地址映射 | 支持 | 支持 + 表达式 | ✅ 更强 |
| Web界面 | 基础 | 现代化 + SSE | ✅ 更好 |
| 价格 | ~$600 | **$0 (开源)** | ✅ 免费 |
| 可定制 | 闭源 | 完全开源 | ✅ 自由 |
| 社区支持 | 有限 | 持续改进 | ✅ 活跃 |

---

## 🚀 快速开始

### 前提条件

- NanoPi R5S 开发板 (或其他ARM64 Linux设备)
- Ubuntu 22.04 ARM64 或 FriendlyWrt
- 测厚传感器 (Modbus RTU, 通过USB-RS485转换器)
- 目标设备: Modbus TCP客户端 或 西门子S7 PLC

### 1. 安装依赖

```bash
# 更新包管理器
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

# 验证安装
pkg-config --modversion libmodbus jsoncpp
```

### 2. 安装 Snap7 (S7协议支持)

```bash
cd /tmp
git clone https://github.com/SCADACS/snap7.git
cd snap7/build/unix

# ARM64编译
make -f arm_v7_linux.mk all

# 安装
sudo cp -r ../bin/arm_v7-linux/* /usr/local/lib/
sudo cp ../../src/sys/*.h /usr/local/include/
sudo ldconfig

# 验证
ls -l /usr/local/lib/libsnap7.so
```

### 3. 编译网关程序

```bash
cd /path/to/17-R5C-wrt

# 构建
chmod +x scripts/build_gateway.sh
./scripts/build_gateway.sh

# 查看生成的可执行文件
ls -l build/gateway-bridge
```

### 4. 配置网关

编辑配置文件 `config/gateway_config.json`:

```json
{
  "gateway": {
    "mode": "modbus_tcp"  // 或 "s7"
  },
  "modbus_rtu": {
    "device": "/dev/ttyUSB0",
    "baudrate": 9600
  },
  "modbus_tcp": {
    "enabled": true,
    "port": 502
  },
  "s7": {
    "enabled": false,
    "plc_ip": "192.168.1.10"
  },
  "mapping_rules": [
    {
      "rule_id": "rule-001",
      "description": "测厚仪 → Modbus TCP",
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

### 5. 运行网关

```bash
# 前台运行 (测试模式)
sudo ./build/gateway-bridge --config config/gateway_config.json

# 后台运行
sudo ./build/gateway-bridge --config config/gateway_config.json --daemon

# 查看日志
tail -f /tmp/gw-test/logs/gateway-bridge.log
```

### 6. 测试连接

#### 测试 Modbus TCP
```bash
# 使用 mbpoll 工具
sudo apt install mbpoll
mbpoll -m tcp -a 1 -r 100 -c 2 -t 3 <网关IP>

# 或使用 Python
pip3 install pymodbus
python3 <<EOF
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('<网关IP>', port=502)
client.connect()

# 读取厚度值 (地址100, 2个寄存器 = 1个Float)
result = client.read_holding_registers(100, 2, unit=1)
if not result.isError():
    import struct
    # 转换为Float (Big Endian)
    value = struct.unpack('>f', struct.pack('>HH', *result.registers))[0]
    print(f"厚度值: {value:.3f} mm")

client.close()
EOF
```

#### 测试 S7 连接
```bash
# 在 TIA Portal 或 Step 7 中查看 DB1.DBD0
# 应该能看到实时更新的厚度值
```

### 7. 访问 Web 界面

```bash
# 在浏览器中打开
http://<网关IP>:8080

# 功能:
# - 查看实时数据
# - 配置映射规则
# - 监控系统状态
# - 查看日志
```

---

## 📁 项目结构

```
17-R5C-wrt/
├── GATEWAY_BRIDGE_DESIGN.md       # 完整技术方案
├── IMPLEMENTATION_GUIDE.md        # 实现指南
├── GATEWAY_README.md              # 本文档
│
├── config/
│   └── gateway_config.json        # 配置文件
│
├── src/
│   └── gateway-bridge/
│       ├── common/                # 公共模块
│       │   ├── types.h           # 数据类型定义
│       │   ├── config.h/cpp      # 配置管理
│       │   └── logger.h/cpp      # 日志系统
│       │
│       ├── protocols/            # 协议实现
│       │   ├── modbus_rtu_master.h/cpp
│       │   ├── modbus_tcp_server.h/cpp
│       │   └── s7_client.h/cpp
│       │
│       ├── mapping/              # 数据映射
│       │   ├── mapping_engine.h/cpp
│       │   └── data_converter.h/cpp
│       │
│       ├── web/                  # Web界面
│       │   ├── web_server.h/cpp
│       │   └── static/           # HTML/CSS/JS
│       │
│       └── main.cpp              # 主程序
│
├── scripts/
│   ├── build_gateway.sh          # 构建脚本
│   ├── install_service.sh        # 安装systemd服务
│   └── test_gateway.sh           # 测试脚本
│
├── systemd/
│   └── gateway-bridge.service    # systemd服务配置
│
└── CMakeLists.txt                # CMake构建配置
```

---

## 📝 配置说明

### 映射规则配置

每个映射规则包含三部分：

#### 1. 源端 (Source) - Modbus RTU
```json
{
  "source": {
    "slave_id": 1,              // RTU从站ID
    "function_code": 3,         // 功能码 (3=读保持寄存器)
    "start_address": 0,         // 起始地址
    "register_count": 2,        // 寄存器数量
    "data_type": "float",       // 数据类型
    "byte_order": "big_endian", // 字节序
    "poll_interval_ms": 100     // 轮询间隔
  }
}
```

#### 2. 目标端 (Destination) - Modbus TCP 或 S7

**Modbus TCP:**
```json
{
  "destination": {
    "protocol": "modbus_tcp",
    "slave_id": 1,
    "function_code": 16,        // 16=写多个寄存器
    "start_address": 100,
    "data_type": "float",
    "byte_order": "big_endian"
  }
}
```

**S7 PLC:**
```json
{
  "destination": {
    "protocol": "s7",
    "db_number": 1,             // DB块号
    "start_byte": 0,            // 字节偏移 (DBD0)
    "bit_offset": 0,            // 位偏移 (for BOOL)
    "data_type": "float"
  }
}
```

#### 3. 数据转换 (Transform)

**缩放:**
```json
{
  "transform": {
    "operation": "scale",
    "scale": 0.001,             // output = input * 0.001
    "offset": 0.0,
    "clamp_enabled": true,
    "min_value": 0.0,
    "max_value": 10.0
  }
}
```

**表达式:**
```json
{
  "transform": {
    "operation": "expression",
    "expression": "x * 1.8 + 32"  // 温度转换 °C to °F
  }
}
```

### 数据类型支持

| 类型 | Modbus寄存器数 | 字节数 | 说明 |
|------|--------------|--------|------|
| `int16` | 1 | 2 | 16位有符号整数 |
| `uint16` | 1 | 2 | 16位无符号整数 |
| `int32` | 2 | 4 | 32位有符号整数 |
| `uint32` | 2 | 4 | 32位无符号整数 |
| `float` | 2 | 4 | 32位浮点数 (IEEE754) |
| `double` | 4 | 8 | 64位浮点数 |

### 字节序支持

| 字节序 | 说明 | 适用场景 |
|--------|------|---------|
| `big_endian` | ABCD | Modbus标准，西门子PLC |
| `little_endian` | DCBA | PC，某些PLC |
| `big_swap` | BADC | 某些日本PLC |
| `little_swap` | CDAB | 某些欧洲PLC |

---

## 🔧 高级功能

### 1. 多规则支持

可以配置多个映射规则，同时运行：

```json
{
  "mapping_rules": [
    {
      "rule_id": "rule-001",
      "description": "厚度 → TCP寄存器100",
      "enabled": true,
      ...
    },
    {
      "rule_id": "rule-002",
      "description": "温度 → S7 DB2.DBD0",
      "enabled": true,
      ...
    },
    {
      "rule_id": "rule-003",
      "description": "压力 → TCP寄存器200",
      "enabled": false,  // 禁用
      ...
    }
  ]
}
```

### 2. 表达式运算

支持复杂的数学表达式：

```json
{
  "transform": {
    "operation": "expression",
    "expression": "sqrt(x^2 + y^2)"  // 向量长度
  }
}
```

支持的运算符和函数：
- 运算符: `+`, `-`, `*`, `/`, `%`, `^` (幂)
- 函数: `sin`, `cos`, `tan`, `sqrt`, `abs`, `log`, `exp`, `floor`, `ceil`
- 变量: `x` (输入值)
- 常量: `pi`, `e`

### 3. 实时监控 API

```bash
# 获取系统状态
curl http://<网关IP>:8080/api/status

# 获取所有规则
curl http://<网关IP>:8080/api/rules

# 获取特定规则
curl http://<网关IP>:8080/api/rules/rule-001

# 启用/禁用规则
curl -X POST http://<网关IP>:8080/api/rules/rule-001/enable
curl -X POST http://<网关IP>:8080/api/rules/rule-001/disable

# 手动触发同步
curl -X POST http://<网关IP>:8080/api/rules/rule-001/sync

# 实时数据流 (SSE)
curl -N http://<网关IP>:8080/api/monitor/realtime
```

---

## 🛠 故障排查

### 问题1: 找不到串口设备

```bash
# 检查USB设备
ls -l /dev/ttyUSB*

# 如果没有，检查驱动
dmesg | grep tty

# 添加权限
sudo chmod 666 /dev/ttyUSB0
# 或添加用户到dialout组
sudo usermod -aG dialout $USER
```

### 问题2: Modbus RTU通信失败

```bash
# 检查串口参数
stty -F /dev/ttyUSB0

# 测试串口
screen /dev/ttyUSB0 9600

# 检查配置文件中的波特率、校验位等
```

### 问题3: Modbus TCP连接失败

```bash
# 检查端口是否监听
sudo netstat -tlnp | grep 502

# 检查防火墙
sudo ufw allow 502/tcp

# 使用tcpdump抓包
sudo tcpdump -i eth0 port 502 -A
```

### 问题4: S7 PLC连接失败

```bash
# 检查网络连接
ping <PLC_IP>

# 检查PLC配置
# - 是否允许PUT/GET通信
# - Rack和Slot是否正确 (通常是0, 1)
# - DB块是否优化 (需要未优化的DB块)

# 使用Snap7测试工具
./snap7_client_test <PLC_IP>
```

---

## 📊 性能测试

### 吞吐量测试

```bash
# Modbus TCP吞吐量
./scripts/test_modbus_throughput.sh

# 预期结果:
# - 轮询频率: 50Hz (20ms间隔)
# - 响应延迟: P99 < 30ms
# - TCP吞吐量: > 1000 TPS
```

### 稳定性测试

```bash
# 长时间运行测试 (24小时)
./scripts/test_stability.sh 24

# 监控指标:
# - 内存占用: < 80MB
# - CPU占用: < 25%
# - 错误率: < 0.1%
```

---

## 🚀 部署到生产环境

### 安装systemd服务

```bash
# 安装服务
sudo ./scripts/install_service.sh

# 启动服务
sudo systemctl start gateway-bridge
sudo systemctl enable gateway-bridge

# 查看状态
sudo systemctl status gateway-bridge

# 查看日志
sudo journalctl -u gateway-bridge -f
```

### 开机自启动

服务已配置为开机自启动，无需额外操作。

### 远程升级

```bash
# 停止服务
sudo systemctl stop gateway-bridge

# 更新程序
sudo cp build/gateway-bridge /usr/local/bin/

# 重启服务
sudo systemctl start gateway-bridge
```

---

## 📚 扩展开发

### 添加新协议

```cpp
// 实现新的协议类
class MQTTClient {
public:
    bool connect(const std::string& broker);
    bool publish(const std::string& topic, const std::string& payload);
};

// 在映射引擎中集成
```

### 添加新的数据转换

```cpp
// 在 TransformRule 中添加新操作
enum class TransformOperation {
    NONE,
    SCALE,
    EXPRESSION,
    LOOKUP,
    CUSTOM    // 新增
};
```

---

## 🤝 贡献指南

欢迎贡献代码、报告问题或提出建议！

### 提交问题
- 在 GitHub Issues 中提交
- 提供详细的错误日志和配置文件

### 提交代码
1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建 Pull Request

---

## 📄 许可证

MIT License - 完全开源，可自由使用和修改。

---

## 📞 支持

- 📧 Email: [your-email]
- 💬 讨论区: GitHub Discussions
- 📖 文档: 查看 `docs/` 目录

---

**开始使用**: `./scripts/build_gateway.sh && sudo ./build/gateway-bridge`

**祝你使用愉快！** 🎉
