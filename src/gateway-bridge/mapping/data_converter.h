#ifndef GATEWAY_BRIDGE_DATA_CONVERTER_H
#define GATEWAY_BRIDGE_DATA_CONVERTER_H

#include "../common/types.h"
#include <vector>
#include <cstdint>

namespace gateway {

/**
 * 数据转换器
 * 负责Modbus寄存器与各种数据类型之间的转换
 */
class DataConverter {
public:
    // Modbus寄存器 → 数值
    static double registers_to_value(const std::vector<uint16_t>& registers,
                                      DataType type, ByteOrder byte_order);

    // 数值 → Modbus寄存器
    static std::vector<uint16_t> value_to_registers(double value, DataType type,
                                                     ByteOrder byte_order);

    // 数值 → S7字节数组
    static std::vector<uint8_t> value_to_s7_bytes(double value, DataType type);

    // S7字节数组 → 数值
    static double s7_bytes_to_value(const std::vector<uint8_t>& bytes, DataType type);

    // 数据转换（应用缩放、偏移、限制）
    static double transform_value(double input, const TransformRule& rule);

    // 字节序转换
    static void swap_byte_order(std::vector<uint16_t>& registers, ByteOrder from, ByteOrder to);

    // 辅助函数：获取数据类型需要的寄存器数量
    static int get_register_count(DataType type);

    // 辅助函数：获取数据类型需要的字节数
    static int get_byte_count(DataType type);

private:
    // IEEE 754 浮点数转换
    static float registers_to_float(const std::vector<uint16_t>& regs, ByteOrder byte_order);
    static void float_to_registers(float value, std::vector<uint16_t>& regs, ByteOrder byte_order);

    // 整数转换
    static int16_t registers_to_int16(const std::vector<uint16_t>& regs);
    static uint16_t registers_to_uint16(const std::vector<uint16_t>& regs);
    static int32_t registers_to_int32(const std::vector<uint16_t>& regs, ByteOrder byte_order);
    static uint32_t registers_to_uint32(const std::vector<uint16_t>& regs, ByteOrder byte_order);

    static void int16_to_registers(int16_t value, std::vector<uint16_t>& regs);
    static void uint16_to_registers(uint16_t value, std::vector<uint16_t>& regs);
    static void int32_to_registers(int32_t value, std::vector<uint16_t>& regs, ByteOrder byte_order);
    static void uint32_to_registers(uint32_t value, std::vector<uint16_t>& regs, ByteOrder byte_order);

    // 限制数值范围
    static double clamp(double value, double min_value, double max_value);
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_DATA_CONVERTER_H
