# 工业协议网关 - 完整技术方案

## 项目目标

打造一个**超越商用网关（如460MMSC）**的开源工业协议转换网关，支持：

- ✅ **Modbus RTU ↔ Modbus TCP** 双向通讯
- ✅ **Modbus RTU ↔ S7** 双向通讯
- ✅ **灵活的地址映射** 和数据类型转换
- ✅ **数学运算支持** (表达式计算)
- ✅ **Web配置界面** 和实时监控
- ✅ **长期稳定运行** (7×24小时)

---

## 一、核心架构设计

### 1.1 系统架构图

```
┌──────────────────────────────────────────────────────────┐
│                  Web 配置界面 (Port 8080)                  │
│  ┌─────────┬─────────┬─────────┬─────────┬─────────┐     │
│  │协议选择 │地址映射 │数据转换 │实时监控 │运算配置 │     │
│  └─────────┴─────────┴─────────┴─────────┴─────────┘     │
├──────────────────────────────────────────────────────────┤
│              协议网关服务 (gateway-bridge)                 │
│                                                           │
│  ┌──────────────┐    ┌──────────────────┐               │
│  │  Modbus RTU  │    │  数据映射引擎     │               │
│  │  Master      │◄──►│  - 地址映射      │               │
│  │  (测厚传感器)│    │  - 类型转换      │               │
│  │              │    │  - 数学运算      │               │
│  └──────────────┘    │  - 缓存管理      │               │
│                      └────────┬─────────┘               │
│                               │                          │
│        ┌──────────────────────┼──────────────────────┐   │
│        ↓                      ↓                      ↓   │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐ │
│  │ Modbus   │         │ S7       │         │ 其他协议 │ │
│  │ TCP      │         │ Client   │         │ (扩展)   │ │
│  │ Server   │         │ (Snap7)  │         │          │ │
│  └──────────┘         └──────────┘         └──────────┘ │
├──────────────────────────────────────────────────────────┤
│                      配置和数据层                          │
│  ┌─────────────┬─────────────┬─────────────────────┐    │
│  │ 配置文件    │ 共享内存     │ 日志系统             │    │
│  │ (JSON)     │ (数据缓存)   │ (syslog/file)       │    │
│  └─────────────┴─────────────┴─────────────────────┘    │
└──────────────────────────────────────────────────────────┘
         ↓                  ↓                  ↓
   /dev/ttyUSB0       Ethernet (502)     Ethernet (S7)
   (RS485测厚仪)      (Modbus TCP)       (西门子PLC)
```

### 1.2 数据流向

#### 场景1: Modbus RTU → Modbus TCP
```
测厚传感器 → RTU读取 → 数据映射 → TCP写入 → PLC/SCADA
(RS485)              (引擎处理)           (客户端连接)
```

#### 场景2: Modbus RTU → S7
```
测厚传感器 → RTU读取 → 数据映射 → S7写入 → 西门子PLC
(RS485)              (引擎处理)          (DB块)
```

#### 场景3: 双向同步
```
测厚传感器 ←→ RTU读写 ←→ 数据映射 ←→ TCP/S7 ←→ PLC
```

---

## 二、核心模块设计

### 2.1 数据映射引擎 (Mapping Engine)

#### 2.1.1 映射配置数据结构

```cpp
// 数据类型枚举
enum DataType {
    TYPE_INT16,      // 16位整数
    TYPE_UINT16,     // 16位无符号整数
    TYPE_INT32,      // 32位整数
    TYPE_UINT32,     // 32位无符号整数
    TYPE_FLOAT,      // 32位浮点数
    TYPE_DOUBLE,     // 64位浮点数
    TYPE_BIT,        // 单个位
    TYPE_STRING      // 字符串
};

// 字节序
enum ByteOrder {
    BYTE_ORDER_BIG_ENDIAN,      // ABCD (Modbus标准)
    BYTE_ORDER_LITTLE_ENDIAN,   // DCBA
    BYTE_ORDER_BIG_SWAP,        // BADC (某些PLC)
    BYTE_ORDER_LITTLE_SWAP      // CDAB
};

// 源端点配置 (Modbus RTU)
struct SourceConfig {
    int slave_id;           // 从站ID
    int function_code;      // 功能码 (3/4/6/16)
    int start_address;      // 起始地址
    int register_count;     // 寄存器数量
    DataType data_type;     // 数据类型
    ByteOrder byte_order;   // 字节序

    // 轮询配置
    int poll_interval_ms;   // 轮询间隔 (毫秒)
    int timeout_ms;         // 超时时间
    int retry_count;        // 重试次数
};

// 目标端点配置 (Modbus TCP 或 S7)
struct DestinationConfig {
    enum ProtocolType {
        PROTOCOL_MODBUS_TCP,
        PROTOCOL_S7
    } protocol;

    // Modbus TCP配置
    struct {
        int slave_id;
        int function_code;
        int start_address;
    } modbus_tcp;

    // S7配置
    struct {
        char plc_ip[16];
        int rack;
        int slot;
        int db_number;
        int start_byte;
        int bit_offset;      // 位偏移 (for BOOL)
    } s7;

    DataType data_type;
    ByteOrder byte_order;
};

// 数据转换规则
struct TransformRule {
    enum Operation {
        OP_NONE,         // 无操作
        OP_SCALE,        // 缩放: output = input * scale + offset
        OP_EXPRESSION,   // 表达式: output = eval(expression)
        OP_LOOKUP        // 查表映射
    } operation;

    // 缩放参数
    double scale;
    double offset;

    // 表达式 (支持: +, -, *, /, sin, cos, sqrt等)
    std::string expression;

    // 查表映射
    std::map<double, double> lookup_table;

    // 范围限制
    double min_value;
    double max_value;
    bool clamp_enabled;
};

// 完整映射规则
struct MappingRule {
    std::string rule_id;          // 规则ID (UUID)
    std::string description;      // 描述
    bool enabled;                 // 是否启用

    SourceConfig source;          // 源配置 (RTU)
    DestinationConfig destination;// 目标配置 (TCP/S7)
    TransformRule transform;      // 转换规则

    // 运行状态
    struct Status {
        uint64_t read_count;      // 读取次数
        uint64_t write_count;     // 写入次数
        uint64_t error_count;     // 错误次数
        uint64_t last_update_ms;  // 最后更新时间
        double last_value;        // 最后的值
        bool is_healthy;          // 健康状态
    } status;
};
```

#### 2.1.2 映射引擎核心类

```cpp
class MappingEngine {
public:
    // 加载配置
    bool load_config(const std::string& config_file);

    // 添加/删除映射规则
    bool add_rule(const MappingRule& rule);
    bool remove_rule(const std::string& rule_id);
    bool update_rule(const std::string& rule_id, const MappingRule& rule);

    // 启动/停止引擎
    bool start();
    bool stop();

    // 获取状态
    std::vector<MappingRule> get_all_rules() const;
    MappingRule get_rule(const std::string& rule_id) const;

    // 手动触发同步
    bool sync_rule(const std::string& rule_id);

private:
    std::vector<MappingRule> rules_;
    std::map<std::string, std::thread> worker_threads_;
    std::atomic<bool> running_;

    // 工作线程函数
    void rule_worker(MappingRule& rule);

    // 数据转换
    double transform_data(double input, const TransformRule& rule);

    // 类型转换
    std::vector<uint16_t> encode_data(double value, DataType type, ByteOrder order);
    double decode_data(const std::vector<uint16_t>& regs, DataType type, ByteOrder order);
};
```

### 2.2 Modbus RTU 主站模块

```cpp
class ModbusRTUMaster {
public:
    ModbusRTUMaster(const std::string& device, int baudrate);
    ~ModbusRTUMaster();

    // 连接/断开
    bool connect();
    bool disconnect();
    bool is_connected() const;

    // 读取操作
    bool read_holding_registers(int slave_id, int address, int count,
                                 std::vector<uint16_t>& data);
    bool read_input_registers(int slave_id, int address, int count,
                               std::vector<uint16_t>& data);
    bool read_coils(int slave_id, int address, int count,
                    std::vector<uint8_t>& data);
    bool read_discrete_inputs(int slave_id, int address, int count,
                               std::vector<uint8_t>& data);

    // 写入操作
    bool write_single_register(int slave_id, int address, uint16_t value);
    bool write_multiple_registers(int slave_id, int address,
                                   const std::vector<uint16_t>& data);
    bool write_single_coil(int slave_id, int address, bool value);

    // 超时和重试配置
    void set_timeout(int timeout_ms);
    void set_retry_count(int count);

private:
    modbus_t* ctx_;
    std::string device_;
    int baudrate_;
    std::mutex mutex_;
};
```

### 2.3 Modbus TCP 服务器模块

```cpp
class ModbusTCPServer {
public:
    ModbusTCPServer(const std::string& listen_ip, int port);
    ~ModbusTCPServer();

    // 启动/停止服务器
    bool start();
    bool stop();
    bool is_running() const;

    // 寄存器映射设置
    bool set_holding_registers(int address, const std::vector<uint16_t>& data);
    bool get_holding_registers(int address, int count, std::vector<uint16_t>& data);

    bool set_input_registers(int address, const std::vector<uint16_t>& data);
    bool set_coils(int address, const std::vector<uint8_t>& data);

    // 回调函数 (当客户端写入时触发)
    using WriteCallback = std::function<void(int address, const std::vector<uint16_t>&)>;
    void set_write_callback(WriteCallback callback);

private:
    modbus_t* ctx_;
    modbus_mapping_t* mapping_;
    std::string listen_ip_;
    int port_;
    std::thread server_thread_;
    std::atomic<bool> running_;
    WriteCallback write_callback_;

    void server_loop();
};
```

### 2.4 S7 客户端模块

```cpp
class S7Client {
public:
    S7Client(const std::string& plc_ip, int rack, int slot);
    ~S7Client();

    // 连接/断开
    bool connect();
    bool disconnect();
    bool is_connected() const;

    // DB块读写
    bool read_db(int db_number, int start_byte, int size, std::vector<uint8_t>& data);
    bool write_db(int db_number, int start_byte, const std::vector<uint8_t>& data);

    // 数据类型读写辅助函数
    bool read_db_real(int db_number, int byte_offset, float& value);
    bool write_db_real(int db_number, int byte_offset, float value);

    bool read_db_dword(int db_number, int byte_offset, uint32_t& value);
    bool write_db_dword(int db_number, int byte_offset, uint32_t value);

    bool read_db_int(int db_number, int byte_offset, int16_t& value);
    bool write_db_int(int db_number, int byte_offset, int16_t value);

    bool read_db_bool(int db_number, int byte_offset, int bit_offset, bool& value);
    bool write_db_bool(int db_number, int byte_offset, int bit_offset, bool value);

    // 超时设置
    void set_timeout(int timeout_ms);

private:
    S7Object client_;
    std::string plc_ip_;
    int rack_;
    int slot_;
    std::mutex mutex_;
};
```

### 2.5 数学表达式引擎

```cpp
class ExpressionEngine {
public:
    // 解析表达式
    bool parse(const std::string& expression);

    // 计算表达式 (x为输入变量)
    double evaluate(double x);

    // 支持的运算符和函数
    // 运算符: +, -, *, /, %, ^(幂)
    // 函数: sin, cos, tan, sqrt, abs, log, exp, floor, ceil
    // 变量: x (输入值)
    // 常量: pi, e

    // 示例表达式:
    // "x * 0.001"              // 缩放
    // "x * 1.8 + 32"           // 温度转换 (°C to °F)
    // "sqrt(x^2 + y^2)"        // 向量长度
    // "x > 100 ? x : 100"      // 三元运算符

private:
    std::string expression_;
    // 使用 tinyexpr 或 muparser 库实现
};
```

---

## 三、配置文件格式

### 3.1 主配置文件 (gateway_config.json)

```json
{
  "version": "1.0",
  "gateway": {
    "name": "Protocol-Gateway-R5S",
    "description": "Industrial Protocol Gateway",
    "mode": "modbus_tcp"
  },

  "modbus_rtu": {
    "device": "/dev/ttyUSB0",
    "baudrate": 9600,
    "parity": "N",
    "data_bits": 8,
    "stop_bits": 1,
    "timeout_ms": 1000,
    "retry_count": 3
  },

  "modbus_tcp": {
    "enabled": true,
    "listen_ip": "0.0.0.0",
    "port": 502,
    "max_connections": 32
  },

  "s7": {
    "enabled": false,
    "plc_ip": "192.168.1.10",
    "rack": 0,
    "slot": 1,
    "connection_timeout_ms": 2000
  },

  "mapping_rules": [
    {
      "rule_id": "rule-001",
      "description": "测厚仪数据 -> Modbus TCP",
      "enabled": true,

      "source": {
        "protocol": "modbus_rtu",
        "slave_id": 1,
        "function_code": 3,
        "start_address": 0,
        "register_count": 2,
        "data_type": "float",
        "byte_order": "big_endian",
        "poll_interval_ms": 100
      },

      "destination": {
        "protocol": "modbus_tcp",
        "slave_id": 1,
        "function_code": 16,
        "start_address": 100,
        "data_type": "float",
        "byte_order": "big_endian"
      },

      "transform": {
        "operation": "scale",
        "scale": 1.0,
        "offset": 0.0,
        "min_value": 0.0,
        "max_value": 100.0,
        "clamp_enabled": true
      }
    },

    {
      "rule_id": "rule-002",
      "description": "测厚仪数据 -> S7 PLC",
      "enabled": false,

      "source": {
        "protocol": "modbus_rtu",
        "slave_id": 1,
        "function_code": 3,
        "start_address": 0,
        "register_count": 2,
        "data_type": "float",
        "byte_order": "big_endian",
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
        "expression": "x * 0.001",
        "min_value": 0.0,
        "max_value": 10.0,
        "clamp_enabled": true
      }
    }
  ],

  "web_server": {
    "enabled": true,
    "port": 8080,
    "auth_enabled": true,
    "username": "admin",
    "password_hash": "$2y$10$..."
  },

  "logging": {
    "level": "INFO",
    "file": "/var/log/gateway-bridge.log",
    "max_size_mb": 10,
    "max_files": 5
  }
}
```

---

## 四、Web 配置界面设计

### 4.1 页面结构

```
┌─────────────────────────────────────────────────────┐
│  [Logo] Protocol Gateway                            │
│  ┌────────┬────────┬────────┬────────┬────────┐    │
│  │仪表板  │映射配置│协议设置│监控    │系统    │    │
│  └────────┴────────┴────────┴────────┴────────┘    │
├─────────────────────────────────────────────────────┤
│                                                     │
│  【仪表板】页面                                      │
│  ┌──────────────┐  ┌──────────────┐               │
│  │ 系统状态     │  │ 协议状态     │               │
│  │ ✅ 运行中    │  │ RTU: 正常    │               │
│  │ CPU: 25%    │  │ TCP: 2个连接 │               │
│  │ 内存: 120MB │  │ S7:  断开    │               │
│  └──────────────┘  └──────────────┘               │
│                                                     │
│  ┌────────────────────────────────────────────┐    │
│  │ 实时数据监控                                │    │
│  │ ┌────────────────────────────────────────┐ │    │
│  │ │ 厚度值: 1.234 mm  [████████░░] 80%    │ │    │
│  │ │ 更新时间: 2025-01-10 10:30:15.123     │ │    │
│  │ │ 读取次数: 12,345  错误: 0             │ │    │
│  │ └────────────────────────────────────────┘ │    │
│  └────────────────────────────────────────────┘    │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 4.2 映射配置页面

```
┌─────────────────────────────────────────────────────┐
│  映射规则配置                         [+ 添加规则]   │
├─────────────────────────────────────────────────────┤
│                                                     │
│  规则 #1: 测厚仪 → Modbus TCP         [✓] [编辑]   │
│  ┌─────────────────────────────────────────────┐   │
│  │ 源端 (Modbus RTU)                           │   │
│  │ 从站ID: 1    功能码: 03 (读保持寄存器)      │   │
│  │ 起始地址: 0   寄存器数: 2                   │   │
│  │ 数据类型: Float32   字节序: Big Endian      │   │
│  │ 轮询间隔: 100ms                             │   │
│  ├─────────────────────────────────────────────┤   │
│  │ 数据转换                                    │   │
│  │ 操作: 缩放   比例: 1.0   偏移: 0.0         │   │
│  │ 范围限制: [0.0, 100.0]   ☑ 启用           │   │
│  ├─────────────────────────────────────────────┤   │
│  │ 目标端 (Modbus TCP)                         │   │
│  │ 从站ID: 1    功能码: 16 (写多个寄存器)      │   │
│  │ 起始地址: 100  寄存器数: 2                  │   │
│  │ 数据类型: Float32   字节序: Big Endian      │   │
│  ├─────────────────────────────────────────────┤   │
│  │ 运行状态                                    │   │
│  │ 读取: 12,345次   写入: 12,340次   错误: 5  │   │
│  │ 最后更新: 2s前   最后值: 1.234             │   │
│  │ 健康状态: ✅ 正常                          │   │
│  └─────────────────────────────────────────────┘   │
│                                                     │
│  规则 #2: 测厚仪 → S7 PLC             [✗] [编辑]   │
│  (折叠状态)                                         │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 4.3 RESTful API 设计

```
GET    /api/system/status          - 获取系统状态
GET    /api/system/info            - 获取系统信息

GET    /api/config                 - 获取完整配置
PUT    /api/config                 - 更新完整配置
POST   /api/config/reload          - 重新加载配置

GET    /api/rules                  - 获取所有映射规则
POST   /api/rules                  - 添加新规则
GET    /api/rules/:id              - 获取指定规则
PUT    /api/rules/:id              - 更新规则
DELETE /api/rules/:id              - 删除规则
POST   /api/rules/:id/enable       - 启用规则
POST   /api/rules/:id/disable      - 禁用规则
POST   /api/rules/:id/sync         - 手动触发同步

GET    /api/protocols/rtu/status   - RTU状态
GET    /api/protocols/tcp/status   - TCP状态
GET    /api/protocols/s7/status    - S7状态

GET    /api/monitor/realtime       - 实时数据 (SSE)
GET    /api/monitor/statistics     - 统计信息
GET    /api/monitor/errors         - 错误日志

GET    /api/logs?lines=100         - 获取日志
```

---

## 五、性能指标

### 5.1 目标性能

| 指标 | 目标值 | 备注 |
|------|--------|------|
| RTU轮询频率 | 10-100Hz可配 | 默认50Hz |
| 端到端延迟 | < 50ms | RTU读取→TCP/S7写入 |
| 映射规则数 | > 100条 | 支持大规模配置 |
| 并发TCP连接 | > 32个 | 多客户端同时访问 |
| 数据吞吐量 | > 1000点/秒 | 单规则数据点 |
| 内存占用 | < 100MB | 稳态运行 |
| CPU占用 | < 30% | 4核平均 |
| 连续运行 | > 720小时 | 30天无重启 |

### 5.2 可靠性保证

- ✅ **异常恢复**: 串口断开自动重连
- ✅ **超时重试**: 可配置重试次数和间隔
- ✅ **健康检查**: 定期检测协议连接状态
- ✅ **看门狗**: 进程崩溃自动重启
- ✅ **配置热更新**: 修改配置无需重启
- ✅ **日志轮转**: 自动清理历史日志

---

## 六、开发计划

### 阶段1: 核心引擎 (1-2周)
- [ ] 数据映射引擎核心逻辑
- [ ] Modbus RTU主站实现
- [ ] Modbus TCP服务器实现
- [ ] S7客户端封装
- [ ] 数学表达式引擎集成
- [ ] 配置文件加载和解析

### 阶段2: Web界面 (1周)
- [ ] Web服务器框架 (civetweb)
- [ ] RESTful API实现
- [ ] 前端页面 (Bootstrap + Vue.js)
- [ ] 实时监控 (Server-Sent Events)

### 阶段3: 测试和优化 (1周)
- [ ] 单元测试
- [ ] 集成测试
- [ ] 压力测试
- [ ] 性能优化
- [ ] 文档编写

### 阶段4: 部署和交付 (3-5天)
- [ ] systemd服务配置
- [ ] 构建脚本和打包
- [ ] 用户手册
- [ ] 示例配置

---

## 七、技术优势对比

### vs 商用网关 (460MMSC)

| 特性 | 460MMSC | 本方案 | 优势 |
|------|---------|--------|------|
| 协议支持 | RTU↔TCP | RTU↔TCP, RTU↔S7, 可扩展 | ✅ 更多协议 |
| 地址映射 | 支持 | 支持 + 数学运算 | ✅ 更灵活 |
| Web界面 | 基础 | 现代化 + 实时监控 | ✅ 更友好 |
| 价格 | ~$600 | 开源免费 | ✅ 零成本 |
| 可定制 | 闭源 | 完全开源 | ✅ 可二次开发 |
| 性能 | 未知 | 明确指标 | ✅ 透明 |
| 社区支持 | 有限 | 开源社区 | ✅ 持续改进 |

---

## 八、总结

这个方案的核心价值在于：

1. **完全开源**: 代码透明，可自由修改
2. **超越商用**: 功能更强大，成本为零
3. **易于扩展**: 模块化设计，轻松添加新协议
4. **工业级可靠**: 7×24小时稳定运行
5. **现代化界面**: Web配置，实时监控

下一步就是开始编码实现！
