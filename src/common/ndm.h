/**
 * @file ndm.h
 * @brief 归一化数据模型 (Normalized Data Model)
 * 
 * 本文件定义了网关系统中所有模块共享的统一数据结构。
 * 所有采集到的数据都会被转换为 NDM 格式，然后通过共享内存传递给其他模块。
 * 
 * 设计目标:
 * - 数据结构统一，避免重复转换
 * - 内存对齐优化，提高访问效率
 * - 包含完整的元数据（时间戳、状态、校验）
 * - 支持高频数据传输（50Hz+）
 * 
 * @author Gateway Project
 * @date 2025-10-10
 */

#ifndef GATEWAY_NDM_H
#define GATEWAY_NDM_H

#include <cstdint>
#include <cstddef>
#include <ctime>

/**
 * @struct NormalizedData
 * @brief 归一化数据结构
 * 
 * 这是系统中所有数据传输的标准格式。
 * 大小: 24 字节，按 64 字节对齐（一个缓存行）
 * 
 * 内存布局:
 * [0-7]   timestamp_ns  - 8字节时间戳
 * [8-11]  sequence      - 4字节序列号
 * [12-15] thickness_mm  - 4字节浮点数
 * [16-17] status        - 2字节状态位
 * [18-19] reserved      - 2字节保留
 * [20]    crc8          - 1字节校验
 * [21-23] padding       - 3字节填充
 */
struct NormalizedData {
    uint64_t timestamp_ns;      ///< 纳秒精度时间戳 (使用 CLOCK_MONOTONIC，不受系统时间调整影响)
    uint32_t sequence;          ///< 数据序列号 (循环递增，用于检测丢失或重复)
    float    thickness_mm;      ///< 厚度值，单位: 毫米 (IEEE754 单精度浮点数)
    uint16_t status;            ///< 状态位 (见下方 NDMStatus 定义)
    uint16_t reserved;          ///< 保留字段，用于未来扩展
    uint8_t  crc8;              ///< 数据完整性校验 (CRC-8/MAXIM)
    uint8_t  padding[3];        ///< 内存对齐填充，确保结构体大小为 24 字节
} __attribute__((packed, aligned(64)));  // packed: 紧凑排列, aligned(64): 按缓存行对齐

/**
 * @namespace NDMStatus
 * @brief 状态位定义
 * 
 * 使用位掩码表示各种状态，可以同时设置多个状态。
 * 低 8 位用于标志位，高 8 位用于错误代码。
 */
namespace NDMStatus {
    constexpr uint16_t DATA_VALID       = 0x0001;  ///< Bit 0: 数据有效标志 (1=有效, 0=无效)
    constexpr uint16_t RS485_OK         = 0x0002;  ///< Bit 1: RS-485 通信正常 (1=正常, 0=异常)
    constexpr uint16_t CRC_OK           = 0x0004;  ///< Bit 2: CRC 校验通过
    constexpr uint16_t SENSOR_OK        = 0x0008;  ///< Bit 3: 传感器状态正常
    constexpr uint16_t ERROR_MASK       = 0xFF00;  ///< Bit 8-15: 错误代码掩码
}

/**
 * @namespace NDMError
 * @brief 错误代码定义
 * 
 * 当发生错误时，错误代码会被存储在 status 字段的高 8 位。
 * 可通过 (status & NDMStatus::ERROR_MASK) 获取错误代码。
 */
namespace NDMError {
    constexpr uint16_t NO_ERROR         = 0x0000;  ///< 无错误
    constexpr uint16_t TIMEOUT          = 0x0100;  ///< 通信超时
    constexpr uint16_t CRC_FAILED       = 0x0200;  ///< CRC 校验失败
    constexpr uint16_t INVALID_FRAME    = 0x0300;  ///< 无效数据帧
    constexpr uint16_t DEVICE_OFFLINE   = 0x0400;  ///< 设备离线
}

/**
 * @brief 获取当前单调时间戳（纳秒）
 * 
 * 使用 CLOCK_MONOTONIC 时钟，不受系统时间调整影响，
 * 适合用于测量时间间隔和计算频率。
 * 
 * @return uint64_t 纳秒精度的时间戳
 * 
 * @note 起始点是系统启动时间，不是 Unix Epoch
 */
inline uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

/**
 * @brief 计算 CRC-8 校验值
 * 
 * 使用 CRC-8/MAXIM 算法（多项式 0x31）
 * 初始值: 0xFF
 * 
 * @param data 待校验数据的指针
 * @param len 数据长度（字节）
 * @return uint8_t CRC-8 校验值
 * 
 * @note 此函数是内联的，编译器会在调用处展开，提高性能
 */
inline uint8_t calculate_crc8(const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    uint8_t crc = 0xFF;  // CRC-8/MAXIM 初始值
    
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;  // 多项式 0x31
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief 为 NDM 数据设置 CRC 校验值
 * 
 * 计算除 crc8 字段外的所有字段的 CRC，并写入 crc8 字段。
 * 
 * @param data NDM 数据结构引用
 * 
 * @note 必须在所有数据字段设置完成后调用此函数
 * 
 * @example
 * NormalizedData data;
 * data.timestamp_ns = get_timestamp_ns();
 * data.thickness_mm = 1.234f;
 * data.status = NDMStatus::DATA_VALID | NDMStatus::RS485_OK;
 * ndm_set_crc(data);  // 计算并设置 CRC
 */
inline void ndm_set_crc(NormalizedData& data) {
    // 计算从结构体开始到 crc8 字段之前的所有数据的 CRC
    data.crc8 = calculate_crc8(&data, offsetof(NormalizedData, crc8));
}

/**
 * @brief 验证 NDM 数据的 CRC 校验
 * 
 * 重新计算 CRC 并与存储的 CRC 值比较。
 * 
 * @param data NDM 数据结构常量引用
 * @return bool true=校验通过, false=校验失败
 * 
 * @note 用于接收端验证数据完整性
 * 
 * @example
 * NormalizedData data;
 * // ... 从共享内存读取数据 ...
 * if (ndm_verify_crc(data)) {
 *     // CRC 校验通过，数据可信
 *     process_data(data);
 * } else {
 *     // CRC 校验失败，数据损坏
 *     log_error("CRC verification failed");
 * }
 */
inline bool ndm_verify_crc(const NormalizedData& data) {
    uint8_t calculated = calculate_crc8(&data, offsetof(NormalizedData, crc8));
    return calculated == data.crc8;
}

#endif // GATEWAY_NDM_H
