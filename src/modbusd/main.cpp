#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include <modbus/modbus.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>

// 全局运行标志
volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    LOG_INFO("Received signal %d, shutting down...", signum);
    g_running = 0;
}

/**
 * Modbus TCP Server
 * Modbus TCP 服务器
 */
class ModbusTCPServer {
public:
    ModbusTCPServer(const std::string& ip, int port)
        : ip_(ip), port_(port), ctx_(nullptr), mapping_(nullptr), socket_(-1) {
    }
    
    ~ModbusTCPServer() {
        stop();
    }
    
    bool start() {
        // 创建 Modbus TCP 上下文
        ctx_ = modbus_new_tcp(ip_.c_str(), port_);
        if (!ctx_) {
            LOG_ERROR("Failed to create Modbus TCP context");
            return false;
        }
        
        // 创建寄存器映射
        // 参数: 线圈数, 离散输入数, 保持寄存器数, 输入寄存器数
        mapping_ = modbus_mapping_new(0, 0, 100, 0);
        if (!mapping_) {
            LOG_ERROR("Failed to create Modbus mapping");
            modbus_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
        
        // 初始化寄存器为 0
        memset(mapping_->tab_registers, 0, 100 * sizeof(uint16_t));
        
        // 监听连接
        socket_ = modbus_tcp_listen(ctx_, 1);
        if (socket_ < 0) {
            LOG_ERROR("Failed to listen on %s:%d", ip_.c_str(), port_);
            modbus_mapping_free(mapping_);
            modbus_free(ctx_);
            mapping_ = nullptr;
            ctx_ = nullptr;
            return false;
        }
        
        LOG_INFO("Modbus TCP server listening on %s:%d", ip_.c_str(), port_);
        return true;
    }
    
    void stop() {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
        }
        
        if (mapping_) {
            modbus_mapping_free(mapping_);
            mapping_ = nullptr;
        }
        
        if (ctx_) {
            modbus_close(ctx_);
            modbus_free(ctx_);
            ctx_ = nullptr;
        }
        
        LOG_INFO("Modbus TCP server stopped");
    }
    
    // 更新寄存器（从共享内存读取数据）
    void update_registers(const NormalizedData& data) {
        if (!mapping_) return;
        
        uint16_t* regs = mapping_->tab_registers;
        
        // 寄存器映射:
        // 40001-40002: Float32 厚度值 (Big-Endian)
        // 40003-40006: Uint64 时间戳 (Big-Endian, Unix ms)
        // 40007:       Uint16 状态位
        // 40008:       Uint16 序列号 (低 16 位)
        
        // Float32 转 2×Uint16 (Big-Endian)
        uint32_t thickness_raw;
        memcpy(&thickness_raw, &data.thickness_mm, 4);
        thickness_raw = __builtin_bswap32(thickness_raw); // 转大端
        regs[0] = (thickness_raw >> 16) & 0xFFFF;
        regs[1] = thickness_raw & 0xFFFF;
        
        // Uint64 时间戳转 Unix ms (Big-Endian)
        uint64_t timestamp_ms = data.timestamp_ns / 1000000;
        timestamp_ms = __builtin_bswap64(timestamp_ms);
        regs[2] = (timestamp_ms >> 48) & 0xFFFF;
        regs[3] = (timestamp_ms >> 32) & 0xFFFF;
        regs[4] = (timestamp_ms >> 16) & 0xFFFF;
        regs[5] = timestamp_ms & 0xFFFF;
        
        // 状态位
        regs[6] = data.status;
        
        // 序列号
        regs[7] = data.sequence & 0xFFFF;
    }
    
    // 处理客户端请求
    void handle_client() {
        if (!ctx_ || socket_ < 0) return;
        
        // 接受客户端连接
        int client_socket = modbus_tcp_accept(ctx_, &socket_);
        if (client_socket < 0) {
            if (g_running) {
                LOG_ERROR("Failed to accept client connection");
            }
            return;
        }
        
        LOG_INFO("Client connected");
        
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        
        while (g_running) {
            // 接收请求
            int rc = modbus_receive(ctx_, query);
            if (rc > 0) {
                // 处理请求
                modbus_reply(ctx_, query, rc, mapping_);
            } else if (rc == -1) {
                // 连接断开或错误
                break;
            }
        }
        
        close(client_socket);
        LOG_INFO("Client disconnected");
    }
    
private:
    std::string ip_;
    int port_;
    modbus_t* ctx_;
    modbus_mapping_t* mapping_;
    int socket_;
};

/**
 * Modbus Daemon Main Function
 */
int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::init("modbusd", false);
    
    LOG_INFO("Modbus TCP Daemon starting...");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 加载配置
    ConfigManager& config = ConfigManager::instance();
    
    std::string config_path = "/opt/gw/conf/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    if (!config.load(config_path)) {
        LOG_WARN("Failed to load config, using defaults");
    }
    
    // 获取 Modbus 配置
    auto modbus_cfg = config.get_modbus_config();
    
    if (!modbus_cfg.enabled) {
        LOG_WARN("Modbus TCP is disabled in config");
        return 0;
    }
    
    LOG_INFO("Modbus Config: listen=%s:%d, slave_id=%d",
             modbus_cfg.listen_ip.c_str(), modbus_cfg.port, modbus_cfg.slave_id);
    
    // 打开共享内存
    SharedMemoryManager shm;
    if (!shm.open()) {
        LOG_FATAL("Failed to open shared memory, is rs485d running?");
        return 1;
    }
    
    RingBuffer* ring = shm.get_ring();
    if (!ring) {
        LOG_FATAL("Failed to get ring buffer");
        return 1;
    }
    
    // 创建 Modbus TCP 服务器
    ModbusTCPServer server(modbus_cfg.listen_ip, modbus_cfg.port);
    if (!server.start()) {
        LOG_FATAL("Failed to start Modbus TCP server");
        return 1;
    }
    
    LOG_INFO("Modbus TCP Daemon started successfully");
    
    // 启动数据更新线程
    std::thread update_thread([&]() {
        NormalizedData data;
        NormalizedData last_data;
        memset(&last_data, 0, sizeof(last_data));
        
        while (g_running) {
            // 从共享内存读取最新数据
            if (ring->pop_latest(data)) {
                // 验证 CRC
                if (ndm_verify_crc(data)) {
                    // 只在数据变化时更新寄存器
                    if (data.sequence != last_data.sequence) {
                        server.update_registers(data);
                        last_data = data;
                    }
                } else {
                    LOG_WARN("CRC verification failed for sequence %u", data.sequence);
                }
            }
            
            // 休眠 10ms
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    // 主循环：处理客户端连接
    while (g_running) {
        server.handle_client();
        
        // 短暂休眠，避免 CPU 占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO("Modbus TCP Daemon shutting down...");
    
    // 等待更新线程结束
    update_thread.join();
    
    // 清理资源
    server.stop();
    shm.close();
    
    LOG_INFO("Modbus TCP Daemon stopped");
    return 0;
}
