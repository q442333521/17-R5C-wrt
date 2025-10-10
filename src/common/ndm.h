#ifndef GATEWAY_NDM_H
#define GATEWAY_NDM_H

#include <cstdint>
#include <ctime>

/**
 * Normalized Data Model (NDM)
 * 统一数据交换结构体 - 24 字节，缓存行对齐
 */
struct NormalizedData {
    uint64_t timestamp_ns;      // 纳秒时间戳 (CLOCK_MONOTONIC)
    uint32_t sequence;          // 序列号 (循环计数)
    float    thickness_mm;      // 厚度值 (mm, IEEE754)
    uint16_t status;            // 状态位 (见下方定义)
    uint16_t reserved;          // 保留字段
    uint8_t  crc8;              // 数据校验
    uint8_t  padding[3];        // 填充到 24 字节
} __attribute__((packed, aligned(64)));

// 状态位定义
namespace NDMStatus {
    constexpr uint16_t DATA_VALID       = 0x0001;  // Bit 0: 数据有效
    constexpr uint16_t RS485_OK         = 0x0002;  // Bit 1: RS-485 通信正常
    constexpr uint16_t CRC_OK           = 0x0004;  // Bit 2: CRC 校验通过
    constexpr uint16_t SENSOR_OK        = 0x0008;  // Bit 3: 传感器正常
    constexpr uint16_t ERROR_MASK       = 0xFF00;  // Bit 8-15: 错误代码
}

// 错误代码
namespace NDMError {
    constexpr uint16_t NO_ERROR         = 0x0000;
    constexpr uint16_t TIMEOUT          = 0x0100;
    constexpr uint16_t CRC_FAILED       = 0x0200;
    constexpr uint16_t INVALID_FRAME    = 0x0300;
    constexpr uint16_t DEVICE_OFFLINE   = 0x0400;
}

// 辅助函数
inline uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

inline uint8_t calculate_crc8(const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    uint8_t crc = 0xFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

inline void ndm_set_crc(NormalizedData& data) {
    data.crc8 = calculate_crc8(&data, offsetof(NormalizedData, crc8));
}

inline bool ndm_verify_crc(const NormalizedData& data) {
    uint8_t calculated = calculate_crc8(&data, offsetof(NormalizedData, crc8));
    return calculated == data.crc8;
}

#endif // GATEWAY_NDM_H
