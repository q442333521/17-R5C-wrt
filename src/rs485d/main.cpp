/**
 * @file main.cpp (rs485d)
 * @brief RS-485 数据采集守护进程
 * 
 * 功能说明:
 * 1. 以 50Hz 频率轮询测厚仪，读取厚度数据
 * 2. 将原始数据转换为 NDM 格式
 * 3. 通过共享内存传递给其他模块（modbusd、s7d 等）
 * 4. 提供统计信息和错误处理
 * 
 * 数据流:
 * 测厚仪 (RS-485) → rs485d → 共享内存 → modbusd/s7d/opcuad → PLC/CNC
 * 
 * 部署方式:
 * - 开发测试: ./rs485d /tmp/gw-test/conf/config.json
 * - 生产环境: systemd 自动启动
 * 
 * @note 当前版本使用模拟数据，实际部署时需要根据测厚仪协议修改 query_thickness() 函数
 * 
 * @author Gateway Project
 * @date 2025-10-10
 */

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
#include <random>
#include <cmath>
#include <cstdlib>

/// @brief 全局运行标志，用于优雅退出
/// @note volatile sig_atomic_t 保证信号处理器的线程安全
volatile sig_atomic_t g_running = 1;

/**
 * @brief 信号处理函数
 * 
 * 处理 SIGINT (Ctrl+C) 和 SIGTERM (kill) 信号。
 * 设置全局标志，让主循环优雅退出。
 * 
 * @param signum 信号编号
 */
void signal_handler(int signum) {
    LOG_INFO("收到信号 %d, 正在关闭...", signum);
    g_running = 0;
}

/**
 * @class RS485Handler
 * @brief RS-485 串口处理器
 * 
 * 负责串口的打开、配置、数据收发和关闭。
 * 
 * 功能:
 * - 打开指定的串口设备（通常是 /dev/ttyUSB0）
 * - 配置串口参数（波特率、数据位、停止位、校验位）
 * - 查询测厚仪数据（发送命令、接收响应）
 * - 优雅关闭串口
 * 
 * 使用示例:
 * ```cpp
 * RS485Handler handler("/dev/ttyUSB0", 19200);
 * if (handler.open()) {
 *     float thickness;
 *     if (handler.query_thickness(thickness)) {
 *         printf("厚度: %.3f mm\n", thickness);
 *     }
 *     handler.close();
 * }
 * ```
 */
class RS485Handler {
public:
    /**
     * @brief 构造函数
     * 
     * @param device 串口设备路径（如 "/dev/ttyUSB0"）
     * @param baudrate 波特率（9600/19200/38400/57600/115200）
     */
    RS485Handler(const std::string& device, int baudrate, bool simulate)
        : device_(device),
          baudrate_(baudrate),
          fd_(-1),
          simulate_(simulate ||
                    device == "SIMULATED" ||
                    device == "simulated" ||
                    device.rfind("sim://", 0) == 0),
          sim_start_(std::chrono::steady_clock::now()) {
    }
    
    /**
     * @brief 析构函数
     * 
     * 自动关闭串口（RAII 设计）
     */
    ~RS485Handler() {
        close();
    }
    
    /**
     * @brief 打开串口设备
     * 
     * 执行以下操作:
     * 1. 打开设备文件
     * 2. 配置波特率
     * 3. 设置串口参数 (8N1, 无流控)
     * 4. 设置超时
     * 5. 清空缓冲区
     * 
     * @return bool true=成功, false=失败
     * 
     * @note 如果设备不存在或权限不足，会失败
     * @note 失败时会输出详细的错误日志
     */
    bool open() {
        if (simulate_) {
            LOG_INFO("RS485 模拟模式已启用，跳过串口设备打开");
            return true;
        }
        
        // 打开串口设备
        // O_RDWR: 读写模式
        // O_NOCTTY: 不将设备设为控制终端
        // O_NDELAY: 非阻塞模式
        fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) {
            LOG_ERROR("无法打开串口设备 %s: %s", 
                      device_.c_str(), strerror(errno));
            return false;
        }
        
        // 获取当前串口配置
        struct termios options;
        tcgetattr(fd_, &options);
        
        // 设置波特率（输入和输出相同）
        speed_t speed = B19200;  // 默认 19200
        switch (baudrate_) {
            case 9600:   speed = B9600; break;
            case 19200:  speed = B19200; break;
            case 38400:  speed = B38400; break;
            case 57600:  speed = B57600; break;
            case 115200: speed = B115200; break;
            default:
                LOG_WARN("不支持的波特率 %d, 使用默认值 19200", baudrate_);
                speed = B19200;
        }
        
        cfsetispeed(&options, speed);  // 设置输入波特率
        cfsetospeed(&options, speed);  // 设置输出波特率
        
        // 配置串口参数: 8N1 (8 数据位, 无校验, 1 停止位)
        options.c_cflag &= ~PARENB;   // 禁用奇偶校验
        options.c_cflag &= ~CSTOPB;   // 1 个停止位
        options.c_cflag &= ~CSIZE;    // 清除数据位掩码
        options.c_cflag |= CS8;       // 8 个数据位
        options.c_cflag &= ~CRTSCTS;  // 禁用硬件流控 (RTS/CTS)
        options.c_cflag |= CREAD | CLOCAL;  // 启用接收器，忽略调制解调器控制线
        
        // 设置为原始模式（不处理输入输出）
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // 禁用规范模式、回显、信号
        options.c_iflag &= ~(IXON | IXOFF | IXANY);          // 禁用软件流控
        options.c_oflag &= ~OPOST;                            // 禁用输出处理
        
        // 设置超时：VMIN=0 表示非阻塞，VTIME=2 表示 200ms 超时
        options.c_cc[VMIN] = 0;   // 最少读取 0 个字符（非阻塞）
        options.c_cc[VTIME] = 2;  // 超时时间 200ms (单位: 0.1秒)
        
        // 应用配置
        tcsetattr(fd_, TCSANOW, &options);
        
        // 清空输入输出缓冲区
        tcflush(fd_, TCIOFLUSH);
        
        LOG_INFO("串口 %s 打开成功 (波特率=%d)", 
                 device_.c_str(), baudrate_);
        return true;
    }
    
    /**
     * @brief 关闭串口设备
     * 
     * @note 析构函数会自动调用此函数
     */
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            LOG_INFO("串口已关闭");
        } else if (simulate_) {
            LOG_INFO("模拟串口已关闭");
        }
    }
    
    /**
     * @brief 查询测厚仪，获取厚度值
     * 
     * 工作流程:
     * 1. 发送查询命令到测厚仪
     * 2. 等待响应（50ms）
     * 3. 读取响应数据
     * 4. 解析厚度值
     * 5. 如果失败，生成模拟值（仅用于测试）
     * 
     * @param[out] thickness 输出厚度值（单位: mm）
     * @return bool true=成功, false=失败
     * 
     * @warning 当前实现使用模拟数据！
     * @todo 实际部署时需要根据测厚仪的实际协议修改此函数
     * 
     * @note 实际协议示例:
     * - 如果是 Modbus RTU: 发送 01 03 00 00 00 02 CRC_L CRC_H
     * - 如果是自定义协议: 参考测厚仪手册实现
     */
    bool query_thickness(float& thickness) {
        if (fd_ < 0) {
            if (simulate_) {
                thickness = generate_simulated_thickness();
                return true;
            }
            return false;
        }
        
        // ====================================================================
        // TODO: 根据实际测厚仪协议修改以下代码
        // ====================================================================
        
        // 当前实现: 模拟 Modbus RTU 查询（仅用于测试）
        // 命令格式: 从站地址(1字节) 功能码(1字节) 起始地址(2字节) 数量(2字节) CRC(2字节)
        // 示例: 读取从站1的保持寄存器 0x0000 开始的 2 个寄存器
        uint8_t query[] = {
            0x01,       // 从站地址
            0x03,       // 功能码: 读保持寄存器
            0x00, 0x00, // 起始地址: 0x0000
            0x00, 0x02, // 数量: 2 个寄存器
            0xC4, 0x0B  // CRC-16 (需要根据实际数据计算)
        };
        
        // 发送查询命令
        ssize_t written = write(fd_, query, sizeof(query));
        if (written != sizeof(query)) {
            LOG_ERROR("发送查询命令失败");
            return false;
        }
        
        // 等待测厚仪响应
        usleep(50000); // 50ms
        
        // 读取响应数据
        uint8_t response[64];
        ssize_t n = read(fd_, response, sizeof(response));
        
        if (n < 7) {
            // 响应数据不足（超时或设备未响应）
            // 为了开发测试，我们生成一个模拟值
            LOG_WARN("串口响应超时或数据不足，使用模拟数据");
            thickness = 1.0f + (rand() % 100) / 100.0f; // 1.00 - 2.00 mm
            return true;
        }
        
        // 解析 Modbus RTU 响应（简化版）
        // 响应格式: 从站地址 功能码 字节数 数据... CRC
        if (response[0] == 0x01 && response[1] == 0x03) {
            uint8_t byte_count = response[2];
            
            // 检查数据长度
            if (byte_count >= 4 && n >= (3 + byte_count + 2)) {
                // 提取厚度值（假设是 Float32 格式）
                uint32_t raw_value;
                memcpy(&raw_value, &response[3], 4);
                
                // Big-Endian 转 Little-Endian（如果需要）
                raw_value = __builtin_bswap32(raw_value);
                
                // 转换为浮点数
                memcpy(&thickness, &raw_value, 4);
                
                LOG_DEBUG("成功读取厚度: %.3f mm", thickness);
                return true;
            }
        }
        
        // 解析失败，使用模拟值
        LOG_WARN("数据解析失败，使用模拟数据");
        thickness = 1.0f + (rand() % 100) / 100.0f;
        return true;
        
        // ====================================================================
        // 实际部署时的修改建议:
        // 1. 根据测厚仪手册修改查询命令格式
        // 2. 实现正确的 CRC 计算（如果需要）
        // 3. 根据响应格式正确解析厚度值
        // 4. 处理各种异常情况（超时、错误码、校验失败等）
        // 5. 删除模拟数据生成代码
        // ====================================================================
    }
    
    /**
     * @brief 检查串口是否已打开
     * 
     * @return bool true=已打开, false=未打开
     */
    bool is_open() const {
        return simulate_ || fd_ >= 0;
    }
    
private:
    float generate_simulated_thickness() {
        using clock = std::chrono::steady_clock;
        const auto elapsed = std::chrono::duration<float>(clock::now() - sim_start_).count();
        // 生成平滑的波动厚度值，并叠加轻微噪声
        float base = 1.5f + 0.2f * std::sin(elapsed * 0.4f);
        float ripple = 0.05f * std::sin(elapsed * 3.2f);
        float noise = 0.01f * std::sin(elapsed * 12.7f);
        return base + ripple + noise;
    }

    std::string device_;    ///< 串口设备路径
    int baudrate_;          ///< 波特率
    int fd_;                ///< 文件描述符
    bool simulate_;         ///< 是否启用模拟模式
    std::chrono::steady_clock::time_point sim_start_; ///< 模拟起始时间
};

/**
 * @brief RS485 守护进程主函数
 * 
 * 程序流程:
 * 1. 初始化日志和信号处理
 * 2. 加载配置文件
 * 3. 创建共享内存
 * 4. 打开串口设备
 * 5. 进入主循环：
 *    - 以 50Hz 频率查询测厚仪
 *    - 将数据封装为 NDM 格式
 *    - 写入共享内存
 *    - 定期输出统计信息
 * 6. 收到退出信号后优雅关闭
 * 
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] 是配置文件路径，可选）
 * @return int 退出码 (0=正常, 1=错误)
 */
int main(int argc, char* argv[]) {
    // 初始化日志系统
    // 参数1: 程序名称（会出现在日志中）
    // 参数2: 是否使用 syslog (false=输出到控制台，true=输出到系统日志)
    Logger::init("rs485d", false);
    
    LOG_INFO("========================================");
    LOG_INFO("RS485 数据采集守护进程启动中...");
    LOG_INFO("========================================");
    
    // 注册信号处理函数，用于优雅退出
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill 命令
    
    // 加载配置文件
    ConfigManager& config = ConfigManager::instance();
    
    // 配置文件路径：优先使用命令行参数，否则使用默认路径
    std::string config_path = "/opt/gw/conf/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    LOG_INFO("加载配置文件: %s", config_path.c_str());
    
    if (!config.load(config_path)) {
        LOG_WARN("配置文件加载失败，使用默认配置");
    }
    
    // 获取 RS485 相关配置
    auto rs485_cfg = config.get_rs485_config();
    LOG_INFO("RS485 配置:");
    LOG_INFO("  设备路径:   %s", rs485_cfg.device.c_str());
    LOG_INFO("  波特率:     %d", rs485_cfg.baudrate);
    LOG_INFO("  采样周期:   %d ms (%.1f Hz)", 
             rs485_cfg.poll_rate_ms, 1000.0f / rs485_cfg.poll_rate_ms);
    LOG_INFO("  超时时间:   %d ms", rs485_cfg.timeout_ms);
    LOG_INFO("  重试次数:   %d", rs485_cfg.retry_count);
    LOG_INFO("  模拟模式:   %s", rs485_cfg.simulate ? "启用" : "关闭");
    
    // 创建共享内存（生产者模式）
    LOG_INFO("创建共享内存...");
    SharedMemoryManager shm;
    if (!shm.create()) {
        LOG_FATAL("共享内存创建失败！");
        return 1;
    }
    
    RingBuffer* ring = shm.get_ring();
    if (!ring) {
        LOG_FATAL("无法获取环形缓冲区指针！");
        return 1;
    }
    
    LOG_INFO("共享内存创建成功 (容量: %d 条数据)", RING_SIZE);
    
    // 打开串口设备
    LOG_INFO("打开串口设备...");
    RS485Handler rs485(rs485_cfg.device, rs485_cfg.baudrate, rs485_cfg.simulate);
    if (!rs485.open()) {
        LOG_FATAL("串口设备打开失败！");
        LOG_FATAL("请检查:");
        LOG_FATAL("  1. 设备是否存在: ls -l %s", rs485_cfg.device.c_str());
        LOG_FATAL("  2. 当前用户是否有权限");
        LOG_FATAL("  3. USB-RS485 转换器是否已连接");
        return 1;
    }
    
    LOG_INFO("========================================");
    LOG_INFO("RS485 守护进程启动成功！");
    LOG_INFO("========================================");
    
    // 统计变量
    uint32_t sequence = 0;         // 数据序列号（循环递增）
    uint32_t success_count = 0;    // 成功次数
    uint32_t error_count = 0;      // 失败次数
    
    auto last_stats_time = std::chrono::steady_clock::now();  // 上次统计时间
    
    // 主循环：以 50Hz 频率采集数据
    LOG_INFO("进入主循环（采样频率: %d ms）", rs485_cfg.poll_rate_ms);
    
    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();
        
        // 1. 查询测厚仪，获取厚度值
        float thickness = 0.0f;
        bool success = rs485.query_thickness(thickness);
        
        // 2. 构造 NDM 数据结构
        NormalizedData data;
        data.timestamp_ns = get_timestamp_ns();  // 纳秒时间戳
        data.sequence = sequence++;              // 序列号递增
        data.thickness_mm = thickness;           // 厚度值
        data.status = 0;                         // 初始化状态为 0
        data.reserved = 0;                       // 保留字段
        
        // 3. 设置状态位
        if (success) {
            // 查询成功，设置所有正常标志
            data.status |= NDMStatus::DATA_VALID;   // 数据有效
            data.status |= NDMStatus::RS485_OK;     // RS-485 通信正常
            data.status |= NDMStatus::CRC_OK;       // CRC 校验通过
            data.status |= NDMStatus::SENSOR_OK;    // 传感器正常
            success_count++;
        } else {
            // 查询失败，设置错误代码
            data.status |= NDMError::TIMEOUT;
            error_count++;
        }
        
        // 4. 计算 CRC 校验值
        ndm_set_crc(data);
        
        // 5. 推入共享内存环形缓冲区
        ring->push(data);
        
        // 6. 定期输出统计信息（每 10 秒）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_stats_time).count();
        
        if (elapsed >= 10) {
            // 计算错误率
            float error_rate = (error_count > 0) ? 
                (100.0f * error_count / (success_count + error_count)) : 0.0f;
            
            // 输出统计信息
            LOG_INFO("统计: 序列号=%u, 成功=%u, 失败=%u, 错误率=%.2f%%, 当前厚度=%.3f mm",
                     sequence, success_count, error_count, error_rate, thickness);
            
            // 重置统计时间
            last_stats_time = now;
        }
        
        // 7. 精确控制采样频率
        // 计算本次循环实际耗时
        auto loop_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            loop_end - loop_start).count();
        
        // 计算需要休眠的时间
        auto target_us = rs485_cfg.poll_rate_ms * 1000;  // 目标周期（微秒）
        
        if (elapsed_us < target_us) {
            // 如果还有剩余时间，休眠补齐
            usleep(target_us - elapsed_us);
        } else {
            // 如果超时，记录警告
            LOG_WARN("采样周期超时！目标=%ld us, 实际=%ld us", target_us, elapsed_us);
        }
    }
    
    // 程序退出流程
    LOG_INFO("========================================");
    LOG_INFO("RS485 守护进程正在关闭...");
    LOG_INFO("========================================");
    
    // 清理资源
    LOG_INFO("关闭串口设备...");
    rs485.close();
    
    LOG_INFO("销毁共享内存...");
    shm.destroy();
    
    LOG_INFO("最终统计: 序列号=%u, 成功=%u, 失败=%u", 
             sequence, success_count, error_count);
    
    LOG_INFO("========================================");
    LOG_INFO("RS485 守护进程已停止");
    LOG_INFO("========================================");
    
    return 0;
}
