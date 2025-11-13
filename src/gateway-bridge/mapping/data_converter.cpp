#include "data_converter.h"
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace gateway {

double DataConverter::registers_to_value(const std::vector<uint16_t>& registers,
                                         DataType type, ByteOrder byte_order) {
    switch (type) {
        case DataType::INT16:
            return static_cast<double>(registers_to_int16(registers));
        case DataType::UINT16:
            return static_cast<double>(registers_to_uint16(registers));
        case DataType::INT32:
            return static_cast<double>(registers_to_int32(registers, byte_order));
        case DataType::UINT32:
            return static_cast<double>(registers_to_uint32(registers, byte_order));
        case DataType::FLOAT:
            return static_cast<double>(registers_to_float(registers, byte_order));
        case DataType::DOUBLE:
            // Double需要4个寄存器，暂不完整实现
            return 0.0;
        default:
            return 0.0;
    }
}

std::vector<uint16_t> DataConverter::value_to_registers(double value, DataType type,
                                                         ByteOrder byte_order) {
    std::vector<uint16_t> registers;

    switch (type) {
        case DataType::INT16:
            int16_to_registers(static_cast<int16_t>(value), registers);
            break;
        case DataType::UINT16:
            uint16_to_registers(static_cast<uint16_t>(value), registers);
            break;
        case DataType::INT32:
            int32_to_registers(static_cast<int32_t>(value), registers, byte_order);
            break;
        case DataType::UINT32:
            uint32_to_registers(static_cast<uint32_t>(value), registers, byte_order);
            break;
        case DataType::FLOAT:
            float_to_registers(static_cast<float>(value), registers, byte_order);
            break;
        default:
            break;
    }

    return registers;
}

std::vector<uint8_t> DataConverter::value_to_s7_bytes(double value, DataType type) {
    std::vector<uint8_t> bytes;

    switch (type) {
        case DataType::INT16: {
            int16_t val = static_cast<int16_t>(value);
            bytes.resize(2);
            bytes[0] = (val >> 8) & 0xFF;
            bytes[1] = val & 0xFF;
            break;
        }
        case DataType::UINT16: {
            uint16_t val = static_cast<uint16_t>(value);
            bytes.resize(2);
            bytes[0] = (val >> 8) & 0xFF;
            bytes[1] = val & 0xFF;
            break;
        }
        case DataType::INT32: {
            int32_t val = static_cast<int32_t>(value);
            bytes.resize(4);
            bytes[0] = (val >> 24) & 0xFF;
            bytes[1] = (val >> 16) & 0xFF;
            bytes[2] = (val >> 8) & 0xFF;
            bytes[3] = val & 0xFF;
            break;
        }
        case DataType::UINT32: {
            uint32_t val = static_cast<uint32_t>(value);
            bytes.resize(4);
            bytes[0] = (val >> 24) & 0xFF;
            bytes[1] = (val >> 16) & 0xFF;
            bytes[2] = (val >> 8) & 0xFF;
            bytes[3] = val & 0xFF;
            break;
        }
        case DataType::FLOAT: {
            float val = static_cast<float>(value);
            uint32_t temp;
            memcpy(&temp, &val, sizeof(float));
            bytes.resize(4);
            bytes[0] = (temp >> 24) & 0xFF;
            bytes[1] = (temp >> 16) & 0xFF;
            bytes[2] = (temp >> 8) & 0xFF;
            bytes[3] = temp & 0xFF;
            break;
        }
        default:
            break;
    }

    return bytes;
}

double DataConverter::s7_bytes_to_value(const std::vector<uint8_t>& bytes, DataType type) {
    if (bytes.empty()) {
        return 0.0;
    }

    switch (type) {
        case DataType::INT16: {
            if (bytes.size() < 2) return 0.0;
            int16_t val = (static_cast<int16_t>(bytes[0]) << 8) | bytes[1];
            return static_cast<double>(val);
        }
        case DataType::UINT16: {
            if (bytes.size() < 2) return 0.0;
            uint16_t val = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
            return static_cast<double>(val);
        }
        case DataType::INT32: {
            if (bytes.size() < 4) return 0.0;
            int32_t val = (static_cast<int32_t>(bytes[0]) << 24) |
                         (static_cast<int32_t>(bytes[1]) << 16) |
                         (static_cast<int32_t>(bytes[2]) << 8) |
                         bytes[3];
            return static_cast<double>(val);
        }
        case DataType::UINT32: {
            if (bytes.size() < 4) return 0.0;
            uint32_t val = (static_cast<uint32_t>(bytes[0]) << 24) |
                          (static_cast<uint32_t>(bytes[1]) << 16) |
                          (static_cast<uint32_t>(bytes[2]) << 8) |
                          bytes[3];
            return static_cast<double>(val);
        }
        case DataType::FLOAT: {
            if (bytes.size() < 4) return 0.0;
            uint32_t temp = (static_cast<uint32_t>(bytes[0]) << 24) |
                           (static_cast<uint32_t>(bytes[1]) << 16) |
                           (static_cast<uint32_t>(bytes[2]) << 8) |
                           bytes[3];
            float val;
            memcpy(&val, &temp, sizeof(float));
            return static_cast<double>(val);
        }
        default:
            return 0.0;
    }
}

double DataConverter::transform_value(double input, const TransformRule& rule) {
    double output = input;

    // 应用转换操作
    switch (rule.operation) {
        case TransformOperation::NONE:
            break;

        case TransformOperation::SCALE:
            output = input * rule.scale + rule.offset;
            break;

        case TransformOperation::EXPRESSION:
            // TODO: 实现表达式引擎
            // 暂时只支持简单的缩放
            output = input * rule.scale + rule.offset;
            break;

        case TransformOperation::LOOKUP:
            // TODO: 实现查表
            break;
    }

    // 应用范围限制
    if (rule.clamp_enabled) {
        output = clamp(output, rule.min_value, rule.max_value);
    }

    return output;
}

void DataConverter::swap_byte_order(std::vector<uint16_t>& registers,
                                    ByteOrder from, ByteOrder to) {
    if (from == to) {
        return;
    }

    // 简化处理：只处理Float (2个寄存器)
    if (registers.size() == 2) {
        if ((from == ByteOrder::BIG_ENDIAN && to == ByteOrder::LITTLE_ENDIAN) ||
            (from == ByteOrder::LITTLE_ENDIAN && to == ByteOrder::BIG_ENDIAN)) {
            std::swap(registers[0], registers[1]);
        } else if ((from == ByteOrder::BIG_ENDIAN && to == ByteOrder::BIG_SWAP) ||
                   (from == ByteOrder::BIG_SWAP && to == ByteOrder::BIG_ENDIAN)) {
            // ABCD <-> BADC
            uint16_t temp = registers[0];
            registers[0] = ((temp & 0xFF) << 8) | ((temp & 0xFF00) >> 8);
            temp = registers[1];
            registers[1] = ((temp & 0xFF) << 8) | ((temp & 0xFF00) >> 8);
        }
    }
}

int DataConverter::get_register_count(DataType type) {
    switch (type) {
        case DataType::INT16:
        case DataType::UINT16:
        case DataType::BIT:
            return 1;
        case DataType::INT32:
        case DataType::UINT32:
        case DataType::FLOAT:
            return 2;
        case DataType::DOUBLE:
            return 4;
        default:
            return 0;
    }
}

int DataConverter::get_byte_count(DataType type) {
    switch (type) {
        case DataType::INT16:
        case DataType::UINT16:
            return 2;
        case DataType::INT32:
        case DataType::UINT32:
        case DataType::FLOAT:
            return 4;
        case DataType::DOUBLE:
            return 8;
        case DataType::BIT:
            return 1;
        default:
            return 0;
    }
}

// 私有辅助函数实现

float DataConverter::registers_to_float(const std::vector<uint16_t>& regs,
                                        ByteOrder byte_order) {
    if (regs.size() < 2) {
        return 0.0f;
    }

    uint32_t temp;

    switch (byte_order) {
        case ByteOrder::BIG_ENDIAN:  // ABCD
            temp = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
            break;
        case ByteOrder::LITTLE_ENDIAN:  // DCBA
            temp = (static_cast<uint32_t>(regs[1]) << 16) | regs[0];
            break;
        case ByteOrder::BIG_SWAP:  // BADC
            temp = (static_cast<uint32_t>(((regs[0] & 0xFF) << 8) | ((regs[0] & 0xFF00) >> 8)) << 16) |
                   (((regs[1] & 0xFF) << 8) | ((regs[1] & 0xFF00) >> 8));
            break;
        case ByteOrder::LITTLE_SWAP:  // CDAB
            temp = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
            temp = ((temp & 0xFF00FF00) >> 8) | ((temp & 0x00FF00FF) << 8);
            break;
        default:
            temp = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
    }

    float result;
    memcpy(&result, &temp, sizeof(float));
    return result;
}

void DataConverter::float_to_registers(float value, std::vector<uint16_t>& regs,
                                       ByteOrder byte_order) {
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));

    regs.resize(2);

    switch (byte_order) {
        case ByteOrder::BIG_ENDIAN:  // ABCD
            regs[0] = (temp >> 16) & 0xFFFF;
            regs[1] = temp & 0xFFFF;
            break;
        case ByteOrder::LITTLE_ENDIAN:  // DCBA
            regs[0] = temp & 0xFFFF;
            regs[1] = (temp >> 16) & 0xFFFF;
            break;
        case ByteOrder::BIG_SWAP:  // BADC
            regs[0] = ((temp >> 24) & 0xFF) | (((temp >> 16) & 0xFF) << 8);
            regs[1] = ((temp >> 8) & 0xFF) | ((temp & 0xFF) << 8);
            break;
        case ByteOrder::LITTLE_SWAP:  // CDAB
            regs[0] = ((temp >> 8) & 0xFF) | ((temp & 0xFF) << 8);
            regs[1] = ((temp >> 24) & 0xFF) | (((temp >> 16) & 0xFF) << 8);
            break;
        default:
            regs[0] = (temp >> 16) & 0xFFFF;
            regs[1] = temp & 0xFFFF;
    }
}

int16_t DataConverter::registers_to_int16(const std::vector<uint16_t>& regs) {
    if (regs.empty()) return 0;
    return static_cast<int16_t>(regs[0]);
}

uint16_t DataConverter::registers_to_uint16(const std::vector<uint16_t>& regs) {
    if (regs.empty()) return 0;
    return regs[0];
}

int32_t DataConverter::registers_to_int32(const std::vector<uint16_t>& regs,
                                         ByteOrder byte_order) {
    if (regs.size() < 2) return 0;

    uint32_t temp;
    if (byte_order == ByteOrder::BIG_ENDIAN) {
        temp = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
    } else {
        temp = (static_cast<uint32_t>(regs[1]) << 16) | regs[0];
    }
    return static_cast<int32_t>(temp);
}

uint32_t DataConverter::registers_to_uint32(const std::vector<uint16_t>& regs,
                                           ByteOrder byte_order) {
    if (regs.size() < 2) return 0;

    if (byte_order == ByteOrder::BIG_ENDIAN) {
        return (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
    } else {
        return (static_cast<uint32_t>(regs[1]) << 16) | regs[0];
    }
}

void DataConverter::int16_to_registers(int16_t value, std::vector<uint16_t>& regs) {
    regs.resize(1);
    regs[0] = static_cast<uint16_t>(value);
}

void DataConverter::uint16_to_registers(uint16_t value, std::vector<uint16_t>& regs) {
    regs.resize(1);
    regs[0] = value;
}

void DataConverter::int32_to_registers(int32_t value, std::vector<uint16_t>& regs,
                                       ByteOrder byte_order) {
    regs.resize(2);
    uint32_t temp = static_cast<uint32_t>(value);

    if (byte_order == ByteOrder::BIG_ENDIAN) {
        regs[0] = (temp >> 16) & 0xFFFF;
        regs[1] = temp & 0xFFFF;
    } else {
        regs[0] = temp & 0xFFFF;
        regs[1] = (temp >> 16) & 0xFFFF;
    }
}

void DataConverter::uint32_to_registers(uint32_t value, std::vector<uint16_t>& regs,
                                        ByteOrder byte_order) {
    regs.resize(2);

    if (byte_order == ByteOrder::BIG_ENDIAN) {
        regs[0] = (value >> 16) & 0xFFFF;
        regs[1] = value & 0xFFFF;
    } else {
        regs[0] = value & 0xFFFF;
        regs[1] = (value >> 16) & 0xFFFF;
    }
}

double DataConverter::clamp(double value, double min_value, double max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

} // namespace gateway
