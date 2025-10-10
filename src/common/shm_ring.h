#ifndef GATEWAY_SHM_RING_H
#define GATEWAY_SHM_RING_H

#include "ndm.h"
#include <atomic>
#include <string>

#define SHM_NAME "/gw_data_ring"
#define RING_SIZE 1024  // 环形缓冲区大小，必须是 2 的幂

/**
 * Lock-Free Ring Buffer
 * 无锁环形缓冲区，支持单生产者多消费者
 */
class RingBuffer {
public:
    std::atomic<uint32_t> write_idx{0};
    std::atomic<uint32_t> read_idx{0};
    NormalizedData data[RING_SIZE];
    
    // 无锁写入 (生产者: rs485d)
    void push(const NormalizedData& d) {
        uint32_t idx = write_idx.fetch_add(1, std::memory_order_relaxed) % RING_SIZE;
        data[idx] = d;
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    // 无锁读取最新数据 (消费者: modbusd/s7d/opcuad)
    bool pop_latest(NormalizedData& d) {
        uint32_t w = write_idx.load(std::memory_order_acquire);
        uint32_t r = read_idx.load(std::memory_order_relaxed);
        
        if (w == r) {
            return false; // 没有新数据
        }
        
        // 读取最新的数据
        uint32_t idx = (w - 1) % RING_SIZE;
        d = data[idx];
        
        // 更新读索引
        read_idx.store(w, std::memory_order_release);
        return true;
    }
    
    // 获取当前数据量
    uint32_t size() const {
        uint32_t w = write_idx.load(std::memory_order_acquire);
        uint32_t r = read_idx.load(std::memory_order_acquire);
        return w - r;
    }
    
    // 检查是否为空
    bool empty() const {
        return size() == 0;
    }
};

/**
 * Shared Memory Manager
 * 共享内存管理器
 */
class SharedMemoryManager {
public:
    SharedMemoryManager();
    ~SharedMemoryManager();
    
    // 创建共享内存 (生产者调用)
    bool create();
    
    // 打开共享内存 (消费者调用)
    bool open();
    
    // 关闭共享内存
    void close();
    
    // 销毁共享内存
    void destroy();
    
    // 获取环形缓冲区指针
    RingBuffer* get_ring() { return ring_; }
    
    // 检查是否已连接
    bool is_connected() const { return ring_ != nullptr; }
    
private:
    int shm_fd_;
    RingBuffer* ring_;
    bool is_creator_;
};

#endif // GATEWAY_SHM_RING_H
