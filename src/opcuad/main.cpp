/**
 * @file main.cpp
 * @brief OPC UA 客户端守护进程
 * 
 * 功能:
 * - 从共享内存读取测厚仪数据
 * - 将数据写入 OPC UA 服务器变量节点
 * - 支持配置热重载
 * - 自动重连机制
 * 
 * OPC UA 通信:
 * - 使用 open62541 库实现
 * - 支持安全模式: None/Sign/SignAndEncrypt
 * - 支持用户名/密码认证
 * 
 * 数据节点映射:
 * - ns=2;s=Gateway.Thickness    - Float - 厚度值 (mm)
 * - ns=2;s=Gateway.Timestamp    - Int64 - 时间戳 (Unix ms)
 * - ns=2;s=Gateway.Status       - UInt16 - 状态位
 * - ns=2;s=Gateway.Sequence     - UInt32 - 序列号
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

// open62541 库头文件 (需要安装: sudo apt install libopen62541-dev)
// 或从 https://open62541.org/ 下载编译
extern "C" {
    #include <open62541/client.h>
    #include <open62541/client_config_default.h>
    #include <open62541/client_highlevel.h>
}

using namespace std::chrono_literals;

// ============================================================================
// 全局变量
// ============================================================================

/// @brief 运行标志,收到信号时设置为0
volatile sig_atomic_t g_running = 1;

/// @brief OPC UA 客户端句柄
UA_Client* g_opcua_client = nullptr;

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
// OPC UA 客户端管理
// ============================================================================

/**
 * @brief 创建 OPC UA 客户端
 * @return bool true=成功, false=失败
 */
bool opcua_create_client() {
    if (g_opcua_client) {
        return true; // 已存在
    }
    
    g_opcua_client = UA_Client_new();
    if (!g_opcua_client) {
        LOG_ERROR("创建 OPC UA 客户端失败");
        return false;
    }
    
    // 使用默认配置
    UA_ClientConfig* config = UA_Client_getConfig(g_opcua_client);
    UA_ClientConfig_setDefault(config);
    
    // 设置超时时间
    config->timeout = 5000; // 5秒
    
    LOG_DEBUG("OPC UA 客户端已创建");
    return true;
}

/**
 * @brief 连接到 OPC UA 服务器
 * 
 * @param server_url 服务器地址 (如 opc.tcp://192.168.1.20:4840)
 * @param username 用户名 (可选)
 * @param password 密码 (可选)
 * @return bool true=连接成功, false=连接失败
 */
bool opcua_connect(const std::string& server_url,
                   const std::string& username = "",
                   const std::string& password = "") {
    if (!opcua_create_client()) {
        return false;
    }
    
    UA_StatusCode retval;
    
    // 匿名连接 或 用户名密码连接
    if (username.empty() || password.empty()) {
        // 匿名连接
        retval = UA_Client_connect(g_opcua_client, server_url.c_str());
    } else {
        // 用户名密码连接
        retval = UA_Client_connectUsername(g_opcua_client,
                                           server_url.c_str(),
                                           username.c_str(),
                                           password.c_str());
    }
    
    if (retval == UA_STATUSCODE_GOOD) {
        LOG_INFO("OPC UA 连接成功: %s", server_url.c_str());
        return true;
    } else {
        LOG_ERROR("OPC UA 连接失败: %s (状态码: 0x%08X)",
                 UA_StatusCode_name(retval), retval);
        return false;
    }
}

/**
 * @brief 断开 OPC UA 连接
 */
void opcua_disconnect() {
    if (g_opcua_client) {
        UA_Client_disconnect(g_opcua_client);
        LOG_INFO("OPC UA 已断开连接");
    }
}

/**
 * @brief 检查 OPC UA 连接状态
 * @return bool true=已连接, false=未连接
 */
bool opcua_is_connected() {
    if (!g_opcua_client) {
        return false;
    }
    
    UA_ClientState state = UA_Client_getState(g_opcua_client);
    return (state == UA_CLIENTSTATE_SESSION ||
            state == UA_CLIENTSTATE_SESSION_RENEWED);
}

/**
 * @brief 写入 Float 变量
 * @param node_id 节点 ID 字符串 (如 "ns=2;s=Gateway.Thickness")
 * @param value 值
 * @return bool true=成功, false=失败
 */
bool opcua_write_float(const std::string& node_id, float value) {
    if (!g_opcua_client || !opcua_is_connected()) {
        return false;
    }
    
    // 解析节点 ID
    UA_NodeId nodeId = UA_NODEID_STRING_ALLOC(2, node_id.c_str() + 6); // 跳过 "ns=2;s="
    
    // 创建变量
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_FLOAT]);
    
    // 写入变量
    UA_StatusCode retval = UA_Client_writeValueAttribute(g_opcua_client, nodeId, &variant);
    
    // 清理
    UA_Variant_clear(&variant);
    UA_NodeId_clear(&nodeId);
    
    if (retval == UA_STATUSCODE_GOOD) {
        return true;
    } else {
        LOG_DEBUG("OPC UA 写入失败 [%s]: %s",
                 node_id.c_str(), UA_StatusCode_name(retval));
        return false;
    }
}

/**
 * @brief 写入 Int64 变量
 * @param node_id 节点 ID 字符串
 * @param value 值
 * @return bool true=成功, false=失败
 */
bool opcua_write_int64(const std::string& node_id, int64_t value) {
    if (!g_opcua_client || !opcua_is_connected()) {
        return false;
    }
    
    UA_NodeId nodeId = UA_NODEID_STRING_ALLOC(2, node_id.c_str() + 6);
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_INT64]);
    
    UA_StatusCode retval = UA_Client_writeValueAttribute(g_opcua_client, nodeId, &variant);
    
    UA_Variant_clear(&variant);
    UA_NodeId_clear(&nodeId);
    
    return (retval == UA_STATUSCODE_GOOD);
}

/**
 * @brief 写入 UInt16 变量
 * @param node_id 节点 ID 字符串
 * @param value 值
 * @return bool true=成功, false=失败
 */
bool opcua_write_uint16(const std::string& node_id, uint16_t value) {
    if (!g_opcua_client || !opcua_is_connected()) {
        return false;
    }
    
    UA_NodeId nodeId = UA_NODEID_STRING_ALLOC(2, node_id.c_str() + 6);
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_UINT16]);
    
    UA_StatusCode retval = UA_Client_writeValueAttribute(g_opcua_client, nodeId, &variant);
    
    UA_Variant_clear(&variant);
    UA_NodeId_clear(&nodeId);
    
    return (retval == UA_STATUSCODE_GOOD);
}

/**
 * @brief 写入 UInt32 变量
 * @param node_id 节点 ID 字符串
 * @param value 值
 * @return bool true=成功, false=失败
 */
bool opcua_write_uint32(const std::string& node_id, uint32_t value) {
    if (!g_opcua_client || !opcua_is_connected()) {
        return false;
    }
    
    UA_NodeId nodeId = UA_NODEID_STRING_ALLOC(2, node_id.c_str() + 6);
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_UINT32]);
    
    UA_StatusCode retval = UA_Client_writeValueAttribute(g_opcua_client, nodeId, &variant);
    
    UA_Variant_clear(&variant);
    UA_NodeId_clear(&nodeId);
    
    return (retval == UA_STATUSCODE_GOOD);
}

/**
 * @brief 写入数据到 OPC UA 服务器
 * 
 * 写入4个变量节点:
 * - ns=2;s=Gateway.Thickness  - 厚度值 (Float)
 * - ns=2;s=Gateway.Timestamp  - 时间戳 (Int64)
 * - ns=2;s=Gateway.Status     - 状态位 (UInt16)
 * - ns=2;s=Gateway.Sequence   - 序列号 (UInt32)
 * 
 * @param data 归一化数据
 * @return bool true=全部写入成功, false=至少一个失败
 */
bool opcua_write_data(const NormalizedData& data) {
    if (!g_opcua_client || !opcua_is_connected()) {
        LOG_WARN("OPC UA 未连接,无法写入数据");
        return false;
    }
    
    bool ok = true;
    
    // 1. 写入厚度值
    if (!opcua_write_float("ns=2;s=Gateway.Thickness", data.thickness_mm)) {
        ok = false;
    }
    
    // 2. 写入时间戳
    if (!opcua_write_int64("ns=2;s=Gateway.Timestamp", static_cast<int64_t>(data.timestamp_ms))) {
        ok = false;
    }
    
    // 3. 写入状态位
    if (!opcua_write_uint16("ns=2;s=Gateway.Status", data.status)) {
        ok = false;
    }
    
    // 4. 写入序列号
    if (!opcua_write_uint32("ns=2;s=Gateway.Sequence", data.sequence)) {
        ok = false;
    }
    
    if (ok) {
        LOG_DEBUG("OPC UA 写入成功: thickness=%.3f mm, seq=%u", data.thickness_mm, data.sequence);
    } else {
        LOG_WARN("OPC UA 写入部分失败");
    }
    
    return ok;
}

// ============================================================================
// 状态写入
// ============================================================================

/**
 * @brief 写入组件状态到状态文件
 * @param sample 数据样本 (可为nullptr)
 * @param connected 是否已连接
 * @param cfg OPC UA配置
 * @param active_protocol 当前激活的协议
 */
static void write_status(const NormalizedData* sample,
                         bool connected,
                         const ConfigManager::OPCUAConfig& cfg,
                         const std::string& active_protocol) {
    Json::Value extra(Json::objectValue);
    Json::Value conf(Json::objectValue);
    conf["enabled"] = cfg.enabled;
    conf["server_url"] = cfg.server_url;
    conf["security_mode"] = cfg.security_mode;
    conf["username"] = cfg.username;
    extra["config"] = conf;
    extra["active_protocol"] = active_protocol;
    extra["connected"] = connected;
    StatusWriter::write_component_status("opcua", sample, connected, extra);
}

// ============================================================================
// 主程序
// ============================================================================

int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::init("opcuad", false);
    
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
    auto opcua_cfg = config.get_opcua_config();
    auto active_protocol = config.get_string("protocol.active", "modbus");
    
    // 配置签名 (用于检测配置变化)
    auto make_signature = [](const ConfigManager::OPCUAConfig& cfg, const std::string& proto) {
        return proto + "|" + (cfg.enabled ? "1" : "0") + "|" + cfg.server_url + "|" +
               cfg.security_mode + "|" + cfg.username + "|" + cfg.password;
    };
    std::string config_signature = make_signature(opcua_cfg, active_protocol);
    
    // 打印启动信息
    LOG_INFO("========================================");
    LOG_INFO("OPC UA 客户端守护进程启动");
    LOG_INFO("========================================");
    LOG_INFO("服务器: %s", opcua_cfg.server_url.c_str());
    LOG_INFO("安全模式: %s", opcua_cfg.security_mode.c_str());
    LOG_INFO("激活协议: %s", active_protocol.c_str());
    LOG_INFO("OPC UA 启用: %s", opcua_cfg.enabled ? "是" : "否");
    
    // 打开共享内存
    SharedMemoryManager shm;
    if (!shm.open()) {
        LOG_ERROR("打开共享内存失败");
        return 1;
    }
    RingBuffer* ring = shm.get_ring();
    
    // 状态变量
    bool is_connected = false;
    bool protocol_active = (active_protocol == "opcua" && opcua_cfg.enabled);
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
    write_status(nullptr, is_connected, opcua_cfg, active_protocol);
    
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
                    auto new_cfg = config.get_opcua_config();
                    auto new_active_protocol = config.get_string("protocol.active", "modbus");
                    auto new_signature = make_signature(new_cfg, new_active_protocol);
                    
                    if (new_signature != config_signature) {
                        LOG_INFO("配置已更新,重新连接...");
                        
                        // 断开旧连接
                        if (is_connected) {
                            opcua_disconnect();
                            is_connected = false;
                        }
                        
                        // 更新配置
                        opcua_cfg = new_cfg;
                        active_protocol = new_active_protocol;
                        protocol_active = (active_protocol == "opcua" && opcua_cfg.enabled);
                        config_signature = new_signature;
                        
                        LOG_INFO("新配置: active=%s, enabled=%s, url=%s",
                                active_protocol.c_str(),
                                opcua_cfg.enabled ? "true" : "false",
                                opcua_cfg.server_url.c_str());
                        
                        write_status(has_data ? &last_data : nullptr, is_connected, opcua_cfg, active_protocol);
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
                LOG_INFO("尝试连接 OPC UA 服务器: %s ...", opcua_cfg.server_url.c_str());
                is_connected = opcua_connect(opcua_cfg.server_url,
                                            opcua_cfg.username,
                                            opcua_cfg.password);
                last_connect_attempt = now;
                write_status(has_data ? &last_data : nullptr, is_connected, opcua_cfg, active_protocol);
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
                    
                    // 如果OPC UA已激活且已连接,写入数据
                    if (protocol_active && is_connected) {
                        bool write_ok = opcua_write_data(data);
                        total_writes++;
                        if (!write_ok) {
                            failed_writes++;
                            // 写入失败可能是连接断开
                            if (!opcua_is_connected()) {
                                LOG_WARN("OPC UA 连接断开,将尝试重连");
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
                LOG_INFO("OPC UA 统计: 总写入=%llu, 失败=%llu, 失败率=%.2f%%",
                        total_writes, failed_writes, error_rate);
            }
            stats_start = now;
            total_writes = 0;
            failed_writes = 0;
        }
        
        // ====================================================================
        // 5. 周期性状态更新
        // ====================================================================
        write_status(has_data ? &last_data : nullptr, is_connected, opcua_cfg, active_protocol);
        
        // 休眠50ms
        std::this_thread::sleep_for(50ms);
    }
    
    // ========================================================================
    // 清理
    // ========================================================================
    
    LOG_INFO("OPC UA 守护进程正在退出...");
    
    // 断开连接
    if (is_connected) {
        opcua_disconnect();
    }
    
    // 销毁客户端
    if (g_opcua_client) {
        UA_Client_delete(g_opcua_client);
        g_opcua_client = nullptr;
    }
    
    // 写入最终状态
    write_status(has_data ? &last_data : nullptr, false, opcua_cfg, active_protocol);
    
    LOG_INFO("OPC UA 守护进程已退出");
    return 0;
}
