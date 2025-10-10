/**
 * @file shm_ring.h
 * @brief 无锁环形缓冲区和共享内存管理
 * 
 * 本文件实现了一个基于 POSIX 共享内存的无锁环形缓冲区，
 * 用于在多个进程之间高效传递数据。
 * 
 * 设计特点:
 * - Lock-Free: 使用原子操作，避免互斥锁开销
 * - SPMC: 单生产者多消费者模式
 * - 零拷贝: 直接在共享内存中读写
 * - 高性能: 支持 50Hz+ 的数据更新频率
 * 
 * 使用场景:
 * - rs485d (生产者) → modbusd/s7d/opcuad (消费者)
 * - 生产者以 50Hz 频率写入数据
 * - 消费者按需读取最新数据
 * 
 * @author Gateway Project
 * @date 2025-10-10
 */

#ifndef GATEWAY_SHM_RING_H
#define GATEWAY_SHM_RING_H

#include "ndm.h"
#include <atomic>
#include <string>

/// @brief 共享内存名称（在 /dev/shm/ 下）
#define SHM_NAME "/gw_data_ring"

/// @brief 环形缓冲区大小（必须是 2 的幂，用于快速取模运算）
#define RING_SIZE 1024

/**
 * @class RingBuffer
 * @brief 无锁环形缓冲区
 * 
 * 使用原子操作实现的单生产者多消费者 (SPMC) 环形缓冲区。
 * 
 * 工作原理:
 * 1. 生产者 (rs485d) 不断写入新数据，write_idx 递增
 * 2. 消费者 (modbusd 等) 读取最新数据，更新自己的 read_idx
 * 3. 使用 memory_order 保证内存可见性
 * 4. 无锁设计避免了互斥锁的性能开销
 * 
 * 内存布局:
 * ```
 * +-------------------+
 * | write_idx (8B)    |  原子变量，生产者写索引
 * | read_idx  (8B)    |  原子变量，消费者读索引
 * | data[0]   (24B)   |  数据数组开始
 * | data[1]   (24B)   |
 * | ...               |
 * | data[1023] (24B)  |  数据数组结束
 * +-------------------+
 * ```
 * 
 * @note 线程安全: 支持单生产者多消费者
 * @note 性能: 无锁设计，读写操作 O(1) 复杂度
 */
class RingBuffer {
public:
    /// @brief 写索引 - 生产者写入位置（原子变量，支持并发访问）
    std::atomic<uint32_t> write_idx{0};
    
    /// @brief 读索引 - 消费者读取位置（原子变量，每个消费者独立维护）
    std::atomic<uint32_t> read_idx{0};
    
    /// @brief 数据数组 - 存储 NDM 数据
    NormalizedData data[RING_SIZE];
    
    /**
     * @brief 写入数据（生产者调用）
     * 
     * 将新数据写入环形缓冲区的下一个位置。
     * 如果缓冲区满了，会覆盖最旧的数据（循环覆盖）。
     * 
     * @param d 要写入的 NDM 数据
     * 
     * @note 线程安全: 仅支持单生产者
     * @note 性能: O(1) 复杂度，无锁操作
     * 
     * @example
     * RingBuffer* ring = ...;
     * NormalizedData data;
     * data.thickness_mm = 1.234f;
     * ring->push(data);  // 写入数据
     */
    void push(const NormalizedData& d) {
        // 原子地获取并递增写索引
        // memory_order_relaxed: 不需要同步其他内存操作
        uint32_t idx = write_idx.fetch_add(1, std::memory_order_relaxed) % RING_SIZE;
        
        // 写入数据到指定位置
        data[idx] = d;
        
        // 内存屏障: 确保数据写入对消费者可见
        // memory_order_release: 之前的所有写操作必须在此之前完成
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    /**
     * @brief 读取最新数据（消费者调用）
     * 
     * 读取环形缓冲区中最新的一条数据。
     * 如果有新数据，返回 true 并填充 d 参数。
     * 如果没有新数据，返回 false。
     * 
     * @param[out] d 用于接收数据的 NDM 结构引用
     * @return bool true=成功读取新数据, false=无新数据
     * 
     * @note 线程安全: 支持多消费者，每个消费者独立维护读索引
     * @note 性能: O(1) 复杂度，无锁操作
     * 
     * @example
     * RingBuffer* ring = ...;
     * NormalizedData data;
     * if (ring->pop_latest(data)) {
     *     // 成功读取到新数据
     *     process_data(data);
     * } else {
     *     // 没有新数据，可以稍后重试
     * }
     */
    bool pop_latest(NormalizedData& d) {
        // 原子地读取写索引
        // memory_order_acquire: 确保能看到生产者的所有写操作
        uint32_t w = write_idx.load(std::memory_order_acquire);
        
        // 读取当前的读索引
        // memory_order_relaxed: 读索引只有本消费者使用
        uint32_t r = read_idx.load(std::memory_order_relaxed);
        
        // 检查是否有新数据
        if (w == r) {
            return false; // 没有新数据
        }
        
        // 读取最新的数据（写索引的前一个位置）
        uint32_t idx = (w - 1) % RING_SIZE;
        d = data[idx];
        
        // 更新读索引到最新位置
        // memory_order_release: 确保数据读取完成后再更新索引
        read_idx.store(w, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 获取当前未读数据量
     * 
     * 计算写索引和读索引的差值，即未读数据的数量。
     * 
     * @return uint32_t 未读数据数量
     * 
     * @note 由于是无锁设计，返回值可能不是精确的，但足够用于监控
     */
    uint32_t size() const {
        uint32_t w = write_idx.load(std::memory_order_acquire);
        uint32_t r = read_idx.load(std::memory_order_acquire);
        return w - r;  // 无符号整数，自动处理溢出
    }
    
    /**
     * @brief 检查缓冲区是否为空
     * 
     * @return bool true=空, false=非空
     */
    bool empty() const {
        return size() == 0;
    }
};

/**
 * @class SharedMemoryManager
 * @brief 共享内存管理器
 * 
 * 负责创建、打开、关闭和销毁共享内存。
 * 
 * 使用方式:
 * - 生产者 (rs485d): 调用 create() 创建共享内存
 * - 消费者 (modbusd 等): 调用 open() 打开已存在的共享内存
 * - 退出时自动清理资源
 * 
 * 共享内存路径: /dev/shm/gw_data_ring
 * 
 * @note RAII 设计: 构造时分配资源，析构时自动释放
 */
class SharedMemoryManager {
public:
    /**
     * @brief 构造函数
     * 
     * 初始化成员变量，但不立即分配共享内存。
     * 需要显式调用 create() 或 open()。
     */
    SharedMemoryManager();
    
    /**
     * @brief 析构函数
     * 
     * 自动关闭共享内存映射和文件描述符。
     */
    ~SharedMemoryManager();
    
    /**
     * @brief 创建共享内存（生产者调用）
     * 
     * 创建新的共享内存区域，如果已存在则先删除。
     * 初始化环形缓冲区的原子变量。
     * 
     * @return bool true=成功, false=失败
     * 
     * @note 仅 rs485d 应该调用此函数
     * @note 如果失败，可通过 strerror(errno) 查看错误信息
     * 
     * @example
     * SharedMemoryManager shm;
     * if (!shm.create()) {
     *     LOG_ERROR("Failed to create shared memory");
     *     return 1;
     * }
     */
    bool create();
    
    /**
     * @brief 打开共享内存（消费者调用）
     * 
     * 打开已存在的共享内存区域。
     * 
     * @return bool true=成功, false=失败（可能是 rs485d 未启动）
     * 
     * @note modbusd、s7d、opcuad 应该调用此函数
     * @note 如果失败，说明生产者还未创建共享内存
     * 
     * @example
     * SharedMemoryManager shm;
     * if (!shm.open()) {
     *     LOG_ERROR("Failed to open shared memory, is rs485d running?");
     *     return 1;
     * }
     */
    bool open();
    
    /**
     * @brief 关闭共享内存
     * 
     * 解除内存映射并关闭文件描述符。
     * 不删除共享内存文件，其他进程仍可访问。
     * 
     * @note 析构函数会自动调用此函数
     */
    void close();
    
    /**
     * @brief 销毁共享内存
     * 
     * 关闭共享内存并删除共享内存文件。
     * 其他进程将无法再访问此共享内存。
     * 
     * @note 仅创建者 (rs485d) 应该调用此函数
     * @note 通常在程序退出时调用
     */
    void destroy();
    
    /**
     * @brief 获取环形缓冲区指针
     * 
     * @return RingBuffer* 环形缓冲区指针，如果未连接则返回 nullptr
     */
    RingBuffer* get_ring() { return ring_; }
    
    /**
     * @brief 检查是否已连接到共享内存
     * 
     * @return bool true=已连接, false=未连接
     */
    bool is_connected() const { return ring_ != nullptr; }
    
private:
    int shm_fd_;            ///< 共享内存文件描述符
    RingBuffer* ring_;      ///< 映射到共享内存的环形缓冲区指针
    bool is_creator_;       ///< 是否是创建者（用于判断是否需要销毁）
};

#endif // GATEWAY_SHM_RING_H
