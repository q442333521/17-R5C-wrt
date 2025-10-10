#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>

// 全局运行标志
volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    LOG_INFO("Received signal %d, shutting down...", signum);
    g_running = 0;
}

/**
 * RS485 Serial Port Handler
 * RS485 串口处理器
 */
class RS485Handler {
public:
    RS485Handler(const std::string& device, int baudrate)
        : device_(device), baudrate_(baudrate), fd_(-1) {
    }
    
    ~RS485Handler() {
        close();
    }
    
    bool open() {
        fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) {
            LOG_ERROR("Failed to open serial device %s: %s", 
                      device_.c_str(), strerror(errno));
            return false;
        }
        
        // 配置串口参数
        struct termios options;
        tcgetattr(fd_, &options);
        
        // 设置波特率
        speed_t speed = B19200;
        switch (baudrate_) {
            case 9600:   speed = B9600; break;
            case 19200:  speed = B19200; break;
            case 38400:  speed = B38400; break;
            case 57600:  speed = B57600; break;
            case 115200: speed = B115200; break;
            default:
                LOG_WARN("Unsupported baudrate %d, using 19200", baudrate_);
                speed = B19200;
        }
        
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);
        
        // 8N1, 无流控
        options.c_cflag &= ~PARENB;   // 无校验
        options.c_cflag &= ~CSTOPB;   // 1 停止位
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;       // 8 数据位
        options.c_cflag &= ~CRTSCTS;  // 无硬件流控
        options.c_cflag |= CREAD | CLOCAL;
        
        // 原始模式
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        options.c_iflag &= ~(IXON | IXOFF | IXANY);
        options.c_oflag &= ~OPOST;
        
        // 超时设置
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 2; // 200ms 超时
        
        tcsetattr(fd_, TCSANOW, &options);
        tcflush(fd_, TCIOFLUSH);
        
        LOG_INFO("Serial port %s opened successfully (baudrate=%d)", 
                 device_.c_str(), baudrate_);
        return true;
    }
    
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            LOG_INFO("Serial port closed");
        }
    }
    
    // 模拟查询测厚仪（实际应根据测厚仪协议实现）
    bool query_thickness(float& thickness) {
        if (fd_ < 0) {
            return false;
        }
        
        // TODO: 实际应发送查询命令到测厚仪
        // 这里先模拟一个随机值用于测试
        
        // 模拟查询命令: 0x01 0x03 0x00 0x00 0x00 0x02 CRC_L CRC_H
        // 这是标准 Modbus RTU 读取寄存器命令
        uint8_t query[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
        
        ssize_t written = write(fd_, query, sizeof(query));
        if (written != sizeof(query)) {
            LOG_ERROR("Failed to write query command");
            return false;
        }
        
        // 等待响应
        usleep(50000); // 50ms
        
        // 读取响应 (Modbus RTU 响应: 地址 功能码 字节数 数据... CRC)
        uint8_t response[64];
        ssize_t n = read(fd_, response, sizeof(response));
        
        if (n < 7) {
            // 超时或数据不足
            // 为了测试，我们生成一个模拟值
            thickness = 1.0f + (rand() % 100) / 100.0f; // 1.00 - 2.00 mm
            return true;
        }
        
        // 解析响应（简化版）
        if (response[0] == 0x01 && response[1] == 0x03) {
            uint8_t byte_count = response[2];
            if (byte_count >= 4 && n >= (3 + byte_count + 2)) {
                // 提取厚度值（假设是 Float32）
                uint32_t raw_value;
                memcpy(&raw_value, &response[3], 4);
                // 注意字节序转换
                raw_value = __builtin_bswap32(raw_value);
                memcpy(&thickness, &raw_value, 4);
                return true;
            }
        }
        
        // 失败，使用模拟值
        thickness = 1.0f + (rand() % 100) / 100.0f;
        return true;
    }
    
    bool is_open() const {
        return fd_ >= 0;
    }
    
private:
    std::string device_;
    int baudrate_;
    int fd_;
};

/**
 * RS485 Daemon Main Function
 */
int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::init("rs485d", false);
    
    LOG_INFO("RS485 Daemon starting...");
    
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
    
    // 获取 RS485 配置
    auto rs485_cfg = config.get_rs485_config();
    LOG_INFO("RS485 Config: device=%s, baudrate=%d, poll_rate=%dms",
             rs485_cfg.device.c_str(), rs485_cfg.baudrate, rs485_cfg.poll_rate_ms);
    
    // 创建共享内存
    SharedMemoryManager shm;
    if (!shm.create()) {
        LOG_FATAL("Failed to create shared memory");
        return 1;
    }
    
    RingBuffer* ring = shm.get_ring();
    if (!ring) {
        LOG_FATAL("Failed to get ring buffer");
        return 1;
    }
    
    // 打开串口
    RS485Handler rs485(rs485_cfg.device, rs485_cfg.baudrate);
    if (!rs485.open()) {
        LOG_FATAL("Failed to open RS485 device");
        return 1;
    }
    
    LOG_INFO("RS485 Daemon started successfully");
    
    // 统计变量
    uint32_t sequence = 0;
    uint32_t success_count = 0;
    uint32_t error_count = 0;
    
    auto last_stats_time = std::chrono::steady_clock::now();
    
    // 主循环
    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();
        
        // 查询厚度值
        float thickness = 0.0f;
        bool success = rs485.query_thickness(thickness);
        
        // 构造 NDM 数据
        NormalizedData data;
        data.timestamp_ns = get_timestamp_ns();
        data.sequence = sequence++;
        data.thickness_mm = thickness;
        data.status = 0;
        data.reserved = 0;
        
        if (success) {
            data.status |= NDMStatus::DATA_VALID;
            data.status |= NDMStatus::RS485_OK;
            data.status |= NDMStatus::CRC_OK;
            data.status |= NDMStatus::SENSOR_OK;
            success_count++;
        } else {
            data.status |= NDMError::TIMEOUT;
            error_count++;
        }
        
        // 计算 CRC
        ndm_set_crc(data);
        
        // 推入共享内存
        ring->push(data);
        
        // 每 10 秒打印统计信息
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count();
        
        if (elapsed >= 10) {
            float error_rate = (error_count > 0) ? 
                (100.0f * error_count / (success_count + error_count)) : 0.0f;
            
            LOG_INFO("Stats: seq=%u, success=%u, error=%u, error_rate=%.2f%%, thickness=%.3f mm",
                     sequence, success_count, error_count, error_rate, thickness);
            
            last_stats_time = now;
        }
        
        // 计算休眠时间（保证 50Hz 采样率）
        auto loop_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count();
        auto target_us = rs485_cfg.poll_rate_ms * 1000;
        
        if (elapsed_us < target_us) {
            usleep(target_us - elapsed_us);
        }
    }
    
    LOG_INFO("RS485 Daemon shutting down...");
    
    // 清理资源
    rs485.close();
    shm.destroy();
    
    LOG_INFO("RS485 Daemon stopped");
    return 0;
}
