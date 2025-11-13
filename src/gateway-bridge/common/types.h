#ifndef GATEWAY_BRIDGE_TYPES_H
#define GATEWAY_BRIDGE_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <atomic>

namespace gateway {

// 数据类型枚举
enum class DataType {
    INT16,      // 16位整数
    UINT16,     // 16位无符号整数
    INT32,      // 32位整数
    UINT32,     // 32位无符号整数
    FLOAT,      // 32位浮点数
    DOUBLE,     // 64位浮点数
    BIT,        // 单个位
    STRING      // 字符串
};

// 字节序
enum class ByteOrder {
    BIG_ENDIAN,      // ABCD (Modbus标准)
    LITTLE_ENDIAN,   // DCBA
    BIG_SWAP,        // BADC (某些PLC)
    LITTLE_SWAP      // CDAB
};

// 协议类型
enum class ProtocolType {
    MODBUS_RTU,
    MODBUS_TCP,
    S7
};

// 转换操作类型
enum class TransformOperation {
    NONE,         // 无操作
    SCALE,        // 缩放: output = input * scale + offset
    EXPRESSION,   // 表达式: output = eval(expression)
    LOOKUP        // 查表映射
};

// Modbus RTU 源配置
struct ModbusRTUSource {
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

// Modbus TCP 目标配置
struct ModbusTCPDestination {
    int slave_id;
    int function_code;
    int start_address;
    DataType data_type;
    ByteOrder byte_order;
};

// S7 目标配置
struct S7Destination {
    std::string plc_ip;
    int rack;
    int slot;
    int db_number;
    int start_byte;
    int bit_offset;      // 位偏移 (for BOOL)
    DataType data_type;
    ByteOrder byte_order;
};

// 目标端点配置
struct DestinationConfig {
    ProtocolType protocol;

    // 协议特定配置
    ModbusTCPDestination modbus_tcp;
    S7Destination s7;
};

// 数据转换规则
struct TransformRule {
    TransformOperation operation;

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

// 映射规则运行状态
struct MappingStatus {
    std::atomic<uint64_t> read_count{0};      // 读取次数
    std::atomic<uint64_t> write_count{0};     // 写入次数
    std::atomic<uint64_t> error_count{0};     // 错误次数
    std::atomic<uint64_t> last_update_ms{0};  // 最后更新时间
    double last_value{0.0};                   // 最后的值
    std::atomic<bool> is_healthy{true};       // 健康状态
};

// 完整映射规则
struct MappingRule {
    std::string rule_id;          // 规则ID (UUID)
    std::string description;      // 描述
    bool enabled;                 // 是否启用

    ModbusRTUSource source;       // 源配置 (RTU)
    DestinationConfig destination;// 目标配置 (TCP/S7)
    TransformRule transform;      // 转换规则

    // 运行状态
    MappingStatus status;
};

// 网关配置
struct GatewayConfig {
    struct {
        std::string name;
        std::string description;
        std::string mode;  // "modbus_tcp" or "s7"
    } gateway;

    struct {
        std::string device;
        int baudrate;
        char parity;
        int data_bits;
        int stop_bits;
        int timeout_ms;
        int retry_count;
    } modbus_rtu;

    struct {
        bool enabled;
        std::string listen_ip;
        int port;
        int max_connections;
    } modbus_tcp;

    struct {
        bool enabled;
        std::string plc_ip;
        int rack;
        int slot;
        int connection_timeout_ms;
    } s7;

    struct {
        bool enabled;
        int port;
        bool auth_enabled;
        std::string username;
        std::string password_hash;
    } web_server;

    struct {
        std::string level;
        std::string file;
        int max_size_mb;
        int max_files;
    } logging;

    std::vector<MappingRule> mapping_rules;
};

// 辅助函数：数据类型转字符串
inline std::string data_type_to_string(DataType type) {
    switch (type) {
        case DataType::INT16: return "int16";
        case DataType::UINT16: return "uint16";
        case DataType::INT32: return "int32";
        case DataType::UINT32: return "uint32";
        case DataType::FLOAT: return "float";
        case DataType::DOUBLE: return "double";
        case DataType::BIT: return "bit";
        case DataType::STRING: return "string";
        default: return "unknown";
    }
}

// 辅助函数：字符串转数据类型
inline DataType string_to_data_type(const std::string& str) {
    if (str == "int16") return DataType::INT16;
    if (str == "uint16") return DataType::UINT16;
    if (str == "int32") return DataType::INT32;
    if (str == "uint32") return DataType::UINT32;
    if (str == "float") return DataType::FLOAT;
    if (str == "double") return DataType::DOUBLE;
    if (str == "bit") return DataType::BIT;
    if (str == "string") return DataType::STRING;
    return DataType::UINT16;  // 默认
}

// 辅助函数：字节序转字符串
inline std::string byte_order_to_string(ByteOrder order) {
    switch (order) {
        case ByteOrder::BIG_ENDIAN: return "big_endian";
        case ByteOrder::LITTLE_ENDIAN: return "little_endian";
        case ByteOrder::BIG_SWAP: return "big_swap";
        case ByteOrder::LITTLE_SWAP: return "little_swap";
        default: return "big_endian";
    }
}

// 辅助函数：字符串转字节序
inline ByteOrder string_to_byte_order(const std::string& str) {
    if (str == "big_endian") return ByteOrder::BIG_ENDIAN;
    if (str == "little_endian") return ByteOrder::LITTLE_ENDIAN;
    if (str == "big_swap") return ByteOrder::BIG_SWAP;
    if (str == "little_swap") return ByteOrder::LITTLE_SWAP;
    return ByteOrder::BIG_ENDIAN;  // 默认
}

} // namespace gateway

#endif // GATEWAY_BRIDGE_TYPES_H
