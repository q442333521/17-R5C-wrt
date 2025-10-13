/**
 * @file main.cpp
 * @brief 西门子 S7 PLC 客户端守护进程
 * 
 * 功能:
 * - 从共享内存读取测厚仪数据
 * - 将数据写入 S7 PLC 的 DB 块
 * - 支持配置热重载
 * - 自动重连机制
 * 
 * S7 通信协议:
 * - 使用 Snap7 库实现
 * - 支持 S7-200/300/400/1200/1500 系列 PLC
 * - TCP/IP 通信,默认端口 102
 * 
 * 数据布局 (写入 PLC DB块):
 * - DB{db_number}.DBD0: Float32 - 厚度值 (mm)
 * - DB{db_number}.DBD4: DWord - 时间戳低32位
 * - DB{db_number}.DBD8: DWord - 时间戳高32位
 * - DB{db_number}.DBW12: Word - 状态位
 * - DB{db_number}.DBW14: Word - 序列号
 * 
 * @author Gateway Project
 * @date 2025-10-11
 * @version 2.0
 */

#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include "../common/status_writer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#include <cstring>
#include <arpa/inet.h>

// Snap7 库头文件 (需要安装: sudo apt install libsnap7-dev)
// 如果没有安装,可以从 https://snap7.sourceforge.net/ 下载
extern "C" {
    #include <snap7.h>
}

using namespace std::chrono_literals;

// ============================================================================
// 全局变量
// ============================================================================

/// @brief 运行标志,收到信号时设置为0
volatile sig_atomic_t g_running = 1;

/// @brief S7 客户端句柄
S7Object g_s7_client = 0;

// ============================================================================
// 信号处理
// ============================================================================

/**
 * @brief 信号处理函数
 * @param signum 信号编号 (SIGINT/SIGTERM)
 */
void signal_handler(int signum) {
    LOG_INFO("收到信号 %d, 正在关闭...", signum);
    g_running = 0;
}

// ============================================================================
// S7 客户端管理
// ============================================================================

/**
 * @brief 连接到 S7 PLC
 * 
 * @param plc_ip PLC IP地址
 * @param rack 机架号 (通常为0)
 * @param slot 槽位号 (S7-300为2, S7-1200/1500为1)
 * @return bool true=连接成功, false=连接失败
 */
bool s7_connect(const std::string& plc_ip, int rack, int slot) {
    if (!g_s7_client) {
        g_s7_client = Cli_Create();
        if (!g_s7_client) {
            LOG_ERROR("创建 S7 客户端失败");
            return false;
        }
    }
    
    // 连接到 PLC (IP, Rack, Slot)
    int result = Cli_ConnectTo(g_s7_client, plc_ip.c_str(), rack, slot);
    if (result == 0) {
        LOG_INFO("S7 连接成功: %s (Rack=%d, Slot=%d)", plc_ip.c_str(), rack, slot);
        return true;
    } else {
        char error_text[256];
        Cli_ErrorText(result, error_text, 256);
        LOG_ERROR("S7 连接失败: %s (错误码: 0x%08X)", error_text, result);
        return false;
    }
}

/**
 * @brief 断开 S7 连接
 */
void s7_disconnect() {
    if (g_s7_client) {
        Cli_Disconnect(g_s7_client);
        LOG_INFO("S7 已断开连接");
    }
}

/**
 * @brief 检查 S7 连接状态
 * @return bool true=已连接, false=未连接
 */
bool s7_is_connected() {
    if (!g_s7_client) {
        return false;
    }
    int connected = 0;
    int status = Cli_GetConnected(g_s7_client, &connected);
    if (status != 0) {
        return false;
    }
    return connected != 0;
}

/**
 * @brief 写入数据到 S7 PLC DB块
 * 
 * 数据布局 (16字节):
 * - Byte 0-3:  Float32 - 厚度值 (Big-Endian)
 * - Byte 4-7:  DWord - 时间戳低32位 (Big-Endian)
 * - Byte 8-11: DWord - 时间戳高32位 (Big-Endian)
 * - Byte 12-13: Word - 状态位 (Big-Endian)
 * - Byte 14-15: Word - 序列号 (Big-Endian)
 * 
 * @param db_number DB块编号 (如 DB10)
 * @param data 归一化数据
 * @return bool true=写入成功, false=写入失败
 */
bool s7_write_data(int db_number, const NormalizedData& data) {
    if (!g_s7_client || !s7_is_connected()) {
        LOG_WARN("S7 未连接,无法写入数据");
        return false;
    }
    
    // 准备数据缓冲区 (16字节)
    uint8_t buffer[16];
    std::memset(buffer, 0, sizeof(buffer));
    
    // 1. 写入厚度值 (Float32, Big-Endian)
    union {
        float f;
        uint32_t u;
    } thickness;
    thickness.f = data.thickness_mm;
    uint32_t thickness_be = htonl(thickness.u);
    std::memcpy(buffer + 0, &thickness_be, 4);
    
    // 2. 写入时间戳 (Uint64, Big-Endian, 分成高低32位)
    uint64_t timestamp_ms = data.timestamp_ns / 1000000ULL;
    uint32_t timestamp_low = htonl(static_cast<uint32_t>(timestamp_ms & 0xFFFFFFFFULL));
    uint32_t timestamp_high = htonl(static_cast<uint32_t>(timestamp_ms >> 32));
    std::memcpy(buffer + 4, &timestamp_low, 4);
    std::memcpy(buffer + 8, &timestamp_high, 4);
    
    // 3. 写入状态位 (Word, Big-Endian)
    uint16_t status_be = htons(data.status);
    std::memcpy(buffer + 12, &status_be, 2);
    
    // 4. 写入序列号 (Word, Big-Endian)
    uint16_t sequence_be = htons(static_cast<uint16_t>(data.sequence & 0xFFFF));
    std::memcpy(buffer + 14, &sequence_be, 2);
    
    // 写入到 PLC (DB块, 起始地址0, 长度16字节)
    int result = Cli_DBWrite(g_s7_client, db_number, 0, 16, buffer);
    if (result == 0) {
        LOG_DEBUG("S7 写入成功: thickness=%.3f mm, seq=%u", data.thickness_mm, data.sequence);
        return true;
    } else {
        char error_text[256];
        Cli_ErrorText(result, error_text, 256);
        LOG_ERROR("S7 写入失败: %s (错误码: 0x%08X)", error_text, result);
        return false;
    }
}

// ============================================================================
// 状态写入
// ============================================================================

/**
 * @brief 写入组件状态到状态文件
 * @param sample 数据样本 (可为nullptr)
 * @param connected 是否已连接
 * @param cfg S7配置
 * @param active_protocol 当前激活的协议
 */
static void write_status(const NormalizedData* sample,
                         bool connected,
                         const ConfigManager::S7Config& cfg,
                         const std::string& active_protocol) {
    Json::Value extra(Json::objectValue);
    Json::Value conf(Json::objectValue);
    conf["enabled"] = cfg.enabled;
    conf["plc_ip"] = cfg.plc_ip;
    conf["rack"] = cfg.rack;
    conf["slot"] = cfg.slot;
    conf["db_number"] = cfg.db_number;
    conf["update_interval_ms"] = cfg.update_interval_ms;
    extra["config"] = conf;
    extra["active_protocol"] = active_protocol;
    extra["connected"] = connected;
    StatusWriter::write_component_status("s7", sample, connected, extra);
}

// ============================================================================
// 主程序
// ============================================================================

int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::init("s7d", false);
    
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
        LOG_WARN("配置加载失败,使用默认配置");
    }
    
    // 获取配置
    auto s7_cfg = config.get_s7_config();
    auto active_protocol = config.get_string("protocol.active", "modbus");
    
    // 配置签名 (用于检测配置变化)
    auto make_signature = [](const ConfigManager::S7Config& cfg, const std::string& proto) {
        return proto + "|" + (cfg.enabled ? "1" : "0") + "|" + cfg.plc_ip + "|" +
               std::to_string(cfg.rack) + "|" + std::to_string(cfg.slot) + "|" +
               std::to_string(cfg.db_number) + "|" + std::to_string(cfg.update_interval_ms);
    };
    std::string config_signature = make_signature(s7_cfg, active_protocol);
    
    // 打印启动信息
    LOG_INFO("========================================");
    LOG_INFO("S7 PLC 客户端守护进程启动");
    LOG_INFO("========================================");
    LOG_INFO("PLC IP: %s", s7_cfg.plc_ip.c_str());
    LOG_INFO("Rack: %d, Slot: %d, DB: %d", s7_cfg.rack, s7_cfg.slot, s7_cfg.db_number);
    LOG_INFO("更新间隔: %d ms", s7_cfg.update_interval_ms);
    LOG_INFO("激活协议: %s", active_protocol.c_str());
    LOG_INFO("S7 启用: %s", s7_cfg.enabled ? "是" : "否");
    
    // 打开共享内存
    SharedMemoryManager shm;
    if (!shm.open()) {
        LOG_ERROR("打开共享内存失败");
        return 1;
    }
    RingBuffer* ring = shm.get_ring();
    
    // 状态变量
    bool is_connected = false;
    bool protocol_active = (active_protocol == "s7" && s7_cfg.enabled);
    NormalizedData last_data{};
    bool has_data = false;
    auto last_reload = std::chrono::steady_clock::now();
    auto last_connect_attempt = std::chrono::steady_clock::now();
    std::filesystem::file_time_type last_mtime{};
    
    // 统计信息
    uint64_t total_writes = 0;
    uint64_t failed_writes = 0;
    auto stats_start = std::chrono::steady_clock::now();
    
    // 写入初始状态
    write_status(nullptr, is_connected, s7_cfg, active_protocol);
    
    // 主循环
    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        
        // ====================================================================
        // 1. 配置热重载 (每1秒检查一次)
        // ====================================================================
        if (now - last_reload > 1s) {
            std::error_code ec;
            auto current_mtime = std::filesystem::last_write_time(config_path, ec);
            if (!ec && current_mtime != last_mtime) {
                last_mtime = current_mtime;
                if (config.load(config_path)) {
                    auto new_cfg = config.get_s7_config();
                    auto new_active_protocol = config.get_string("protocol.active", "modbus");
                    auto new_signature = make_signature(new_cfg, new_active_protocol);
                    
                    if (new_signature != config_signature) {
                        LOG_INFO("配置已更新,重新连接...");
                        
                        // 断开旧连接
                        if (is_connected) {
                            s7_disconnect();
                            is_connected = false;
                        }
                        
                        // 更新配置
                        s7_cfg = new_cfg;
                        active_protocol = new_active_protocol;
                        protocol_active = (active_protocol == "s7" && s7_cfg.enabled);
                        config_signature = new_signature;
                        
                        LOG_INFO("新配置: active=%s, enabled=%s, ip=%s",
                                active_protocol.c_str(),
                                s7_cfg.enabled ? "true" : "false",
                                s7_cfg.plc_ip.c_str());
                        
                        write_status(has_data ? &last_data : nullptr, is_connected, s7_cfg, active_protocol);
                    }
                }
            }
            last_reload = now;
        }
        
        // ====================================================================
        // 2. 连接管理 (每5秒尝试一次重连)
        // ====================================================================
        if (protocol_active && !is_connected) {
            if (now - last_connect_attempt > 5s) {
                LOG_INFO("尝试连接 PLC: %s ...", s7_cfg.plc_ip.c_str());
                is_connected = s7_connect(s7_cfg.plc_ip, s7_cfg.rack, s7_cfg.slot);
                last_connect_attempt = now;
                write_status(has_data ? &last_data : nullptr, is_connected, s7_cfg, active_protocol);
            }
        }
        
        // ====================================================================
        // 3. 数据读取和写入
        // ====================================================================
        if (ring) {
            NormalizedData data;
            if (ring->pop_latest(data)) {
                // 验证CRC
                if (ndm_verify_crc(data)) {
                    last_data = data;
                    has_data = true;
                    
                    // 如果S7已激活且已连接,写入数据
                    if (protocol_active && is_connected) {
                        bool write_ok = s7_write_data(s7_cfg.db_number, data);
                        total_writes++;
                        if (!write_ok) {
                            failed_writes++;
                            // 写入失败可能是连接断开
                            if (!s7_is_connected()) {
                                LOG_WARN("S7 连接断开,将尝试重连");
                                is_connected = false;
                            }
                        }
                    }
                }
            }
        }
        
        // ====================================================================
        // 4. 统计信息 (每10秒输出一次)
        // ====================================================================
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stats_start).count();
        if (elapsed >= 10) {
            if (protocol_active && total_writes > 0) {
                double error_rate = (double)failed_writes / total_writes * 100.0;
                LOG_INFO("S7 统计: 总写入=%llu, 失败=%llu, 失败率=%.2f%%",
                        static_cast<unsigned long long>(total_writes),
                        static_cast<unsigned long long>(failed_writes),
                        error_rate);
            }
            stats_start = now;
            total_writes = 0;
            failed_writes = 0;
        }
        
        // ====================================================================
        // 5. 周期性状态更新
        // ====================================================================
        write_status(has_data ? &last_data : nullptr, is_connected, s7_cfg, active_protocol);
        
        // 休眠一段时间 (根据配置的更新间隔)
        std::this_thread::sleep_for(std::chrono::milliseconds(s7_cfg.update_interval_ms));
    }
    
    // ========================================================================
    // 清理
    // ========================================================================
    
    LOG_INFO("S7 守护进程正在退出...");
    
    // 断开连接
    if (is_connected) {
        s7_disconnect();
    }
    
    // 销毁客户端
    if (g_s7_client) {
        Cli_Destroy(&g_s7_client);
    }
    
    // 写入最终状态
    write_status(has_data ? &last_data : nullptr, false, s7_cfg, active_protocol);
    
    LOG_INFO("S7 守护进程已退出");
    return 0;
}
