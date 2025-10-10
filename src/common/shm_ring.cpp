#include "shm_ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

SharedMemoryManager::SharedMemoryManager() 
    : shm_fd_(-1), ring_(nullptr), is_creator_(false) {
}

SharedMemoryManager::~SharedMemoryManager() {
    close();
}

bool SharedMemoryManager::create() {
    // 先尝试删除已存在的共享内存
    shm_unlink(SHM_NAME);
    
    // 创建新的共享内存
    shm_fd_ = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_ < 0) {
        std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置大小
    if (ftruncate(shm_fd_, sizeof(RingBuffer)) < 0) {
        std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
        ::close(shm_fd_);
        shm_unlink(SHM_NAME);
        return false;
    }
    
    // 映射内存
    ring_ = static_cast<RingBuffer*>(mmap(nullptr, sizeof(RingBuffer),
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED, shm_fd_, 0));
    
    if (ring_ == MAP_FAILED) {
        std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
        ::close(shm_fd_);
        shm_unlink(SHM_NAME);
        ring_ = nullptr;
        return false;
    }
    
    // 初始化原子变量
    ring_->write_idx.store(0);
    ring_->read_idx.store(0);
    
    is_creator_ = true;
    std::cout << "Shared memory created successfully" << std::endl;
    return true;
}

bool SharedMemoryManager::open() {
    // 打开已存在的共享内存
    shm_fd_ = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd_ < 0) {
        std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 映射内存
    ring_ = static_cast<RingBuffer*>(mmap(nullptr, sizeof(RingBuffer),
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED, shm_fd_, 0));
    
    if (ring_ == MAP_FAILED) {
        std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
        ::close(shm_fd_);
        ring_ = nullptr;
        return false;
    }
    
    is_creator_ = false;
    std::cout << "Shared memory opened successfully" << std::endl;
    return true;
}

void SharedMemoryManager::close() {
    if (ring_ != nullptr && ring_ != MAP_FAILED) {
        munmap(ring_, sizeof(RingBuffer));
        ring_ = nullptr;
    }
    
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

void SharedMemoryManager::destroy() {
    close();
    
    if (is_creator_) {
        shm_unlink(SHM_NAME);
        std::cout << "Shared memory destroyed" << std::endl;
    }
}
